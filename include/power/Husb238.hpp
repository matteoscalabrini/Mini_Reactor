/*
 * Husb238.hpp — Public interface for the HUSB238 USB Power Delivery sink driver.
 *
 * Declares the Husb238 class, which wraps I2C communication with the Hynetek
 * HUSB238 USB Type-C PD sink controller IC.
 *
 * The HUSB238 is a standalone USB PD 3.0 / PD 2.0 sink controller.  It
 * autonomously handles the CC line negotiation state machine and exposes its
 * status and control registers over a 400 kHz I2C bus at the fixed address 0x08.
 * The host firmware uses this driver to:
 *   - Read the currently negotiated VBUS voltage and advertised current.
 *   - Inspect per-voltage source capability (PDO) availability and current limits.
 *   - Request a specific PDO profile from the charger (power source).
 *   - Issue a GET_SOURCE_CAPABILITIES PD message to refresh the PDO table.
 *   - Trigger a USB PD Hard Reset to re-enumerate the attached source.
 *
 * Communication model:
 *   Register reads and writes use a standard I2C byte-addressed protocol.
 *   PD commands (request profile, get capabilities, hard reset) are issued by
 *   writing a command byte to the GO_COMMAND register (0x09); the HUSB238 then
 *   independently handles the PD protocol and updates its status registers.
 *
 * Unlike the other power drivers in this codebase, Husb238 does not have an
 * enable/disable Config struct — it is always initialised if begin() is called.
 *
 * Implementation: src/power/Husb238.cpp
 */

#pragma once

#include <Arduino.h>
#include <Wire.h>

class Husb238 {
 public:

  // ─── ERROR CODES ────────────────────────────────────────────────────────────

  /*
   * Error — strongly-typed error codes returned by lastError().
   *   None                    : no error / operation succeeded.
   *   DeviceNotFound          : no ACK received at the HUSB238's I2C address.
   *   I2cWriteFailed          : endTransmission() returned non-zero.
   *   I2cReadFailed           : requestFrom() returned the wrong byte count.
   *   InvalidArgument         : called with an unsupported PdSelection value or
   *                             other invalid argument.
   */
  enum class Error : uint8_t {
    None = 0,
    DeviceNotFound,
    I2cWriteFailed,
    I2cReadFailed,
    InvalidArgument,
  };

  // ─── RESPONSE CODE ENUM ──────────────────────────────────────────────────────

  /*
   * ResponseCode — PD command response status read from PD_STATUS1 bits [4:2].
   * Reflects whether the last GO_COMMAND action succeeded at the PD protocol
   * layer.  Values are defined by the HUSB238 register specification.
   *
   *   NoResponse               : no response received yet (initial / idle state).
   *   Success                  : last command completed successfully.
   *   InvalidCommandOrArgument : the command byte or the selected PDO was not
   *                              valid for the current connection state.
   *   CommandNotSupported      : the source does not support the requested PDO.
   *   TransactionFailedNoGoodCrc : the PD transaction failed because the source
   *                              did not return a GoodCRC acknowledgement; may
   *                              indicate a noisy cable or incompatible charger.
   */
  enum class ResponseCode : uint8_t {
    NoResponse = 0b000,
    Success = 0b001,
    InvalidCommandOrArgument = 0b011,
    CommandNotSupported = 0b100,
    TransactionFailedNoGoodCrc = 0b101,
  };

  // ─── VOLTAGE CODE ENUM ───────────────────────────────────────────────────────

  /*
   * VoltageCode — negotiated VBUS voltage, read from PD_STATUS0 bits [7:4].
   * Reflects the voltage currently being delivered by the attached PD source.
   *
   *   Unattached : no PD source attached (CC unconnected or below vRd threshold).
   *   V5  :  5 V — USB default power or PD fixed 5 V PDO.
   *   V9  :  9 V — PD fixed 9 V PDO.
   *   V12 : 12 V — PD fixed 12 V PDO.
   *   V15 : 15 V — PD fixed 15 V PDO.
   *   V18 : 18 V — PD fixed 18 V PDO.
   *   V20 : 20 V — PD fixed 20 V PDO.
   */
  enum class VoltageCode : uint8_t {
    Unattached = 0b0000,
    V5 = 0b0001,
    V9 = 0b0010,
    V12 = 0b0011,
    V15 = 0b0100,
    V18 = 0b0101,
    V20 = 0b0110,
  };

  // ─── PD SELECTION ENUM ───────────────────────────────────────────────────────

  /*
   * PdSelection — identifies a USB PD fixed-voltage source PDO, used both to
   * read per-voltage source capability registers and to request a specific
   * profile via requestProfile().
   *
   * Values match the SRC_PDO_SELECT register encoding (0x08 bits [3:0]).
   *
   *   PD_NOT_SELECTED : no profile selected (reset / default state).
   *   PD_SRC_5V       : select / query the 5 V fixed PDO.
   *   PD_SRC_9V       : select / query the 9 V fixed PDO.
   *   PD_SRC_12V      : select / query the 12 V fixed PDO.
   *   PD_SRC_15V      : select / query the 15 V fixed PDO.
   *   PD_SRC_18V      : select / query the 18 V fixed PDO.
   *   PD_SRC_20V      : select / query the 20 V fixed PDO.
   */
  enum class PdSelection : uint8_t {
    PD_NOT_SELECTED = 0b0000,
    PD_SRC_5V = 0b0001,
    PD_SRC_9V = 0b0010,
    PD_SRC_12V = 0b0011,
    PD_SRC_15V = 0b1000,
    PD_SRC_18V = 0b1001,
    PD_SRC_20V = 0b1010,
  };

  // ─── STATUS STRUCT ───────────────────────────────────────────────────────────

  /*
   * Status — snapshot of PD connection state populated by refreshStatus().
   *
   *   attached    : true when a USB Type-C source is attached and the CC
   *                 handshake has completed (ATTACH bit in PD_STATUS1).
   *   cc2Connected: true when the cable is oriented so that CC2 is the active
   *                 CC line (CC2 bit in PD_STATUS1).  CC1 is active when false.
   *   response    : result of the last PD command (see ResponseCode enum).
   *                 Read from PD_STATUS1 bits [4:2].
   *   voltage     : currently negotiated VBUS voltage (see VoltageCode enum).
   *                 Read from PD_STATUS0 bits [7:4].
   *   currentCode : negotiated current capability code from PD_STATUS0 bits [1:0].
   *                 0 = 0.9 A, 1 = 1.5 A, 2 = 2.0 A, 3 = 3.0 A (per HUSB238
   *                 datasheet Table 4).
   *   rawStatus0  : raw byte of PD_STATUS0 register (0x00) for diagnostics.
   *   rawStatus1  : raw byte of PD_STATUS1 register (0x01) for diagnostics.
   */
  struct Status {
    bool attached = false;
    bool cc2Connected = false;
    ResponseCode response = ResponseCode::NoResponse;
    VoltageCode voltage = VoltageCode::Unattached;
    uint8_t currentCode = 0;
    uint8_t rawStatus0 = 0;
    uint8_t rawStatus1 = 0;
  };

  // ─── SOURCE CAPABILITY STRUCT ─────────────────────────────────────────────────

  /*
   * SourceCapability — per-voltage PDO availability and current limit,
   * populated by readSourceCapability() for a given PdSelection.
   *
   *   present     : true if the attached source advertises this voltage PDO.
   *                 Corresponds to bit [7] of the SRC_PDO_xV register.
   *   currentCode : maximum current code advertised for this PDO in bits [4:0].
   *                 Decoded using the same table as Status::currentCode.
   *   raw         : raw SRC_PDO register byte for diagnostics.
   */
  struct SourceCapability {
    bool present = false;
    uint8_t currentCode = 0;
    uint8_t raw = 0;
  };

  // ─── PUBLIC API ──────────────────────────────────────────────────────────────

  /*
   * Constructor — takes a TwoWire reference and the I2C device address.
   * The HUSB238 has a fixed I2C address of 0x08.  Hardware initialisation
   * is deferred to begin().
   */
  explicit Husb238(TwoWire& wire = Wire, uint8_t address = 0x08);

  /*
   * begin() — Start the I2C bus on the specified pins and clock frequency and
   * attempt to probe the HUSB238 (ACK check only; no part-ID verification).
   * Must be called before any other method.
   * Returns true if the bus started and the device acknowledged; false otherwise.
   */
  bool begin(int sdaPin, int sclPin, uint32_t frequencyHz);

  /*
   * probe() — Send an I2C address probe (zero-byte write) and confirm ACK.
   * Does not read or modify any registers.  Useful for detecting hot-plug events
   * or verifying the bus after an error recovery.
   * Returns true if the HUSB238 acknowledges; false otherwise.
   */
  bool probe();

  /*
   * refreshStatus() — Read PD_STATUS0 (0x00) and PD_STATUS1 (0x01) and decode
   * the attached flag, CC orientation, response code, voltage, and current into
   * outStatus.
   * Returns true on success; false on I2C error (lastError() is set).
   */
  bool refreshStatus(Status& outStatus);

  /*
   * readSourceCapability() — Read the SRC_PDO register for the given PdSelection
   * and decode PDO availability and current limit into outCapability.
   * Returns true on success; false on I2C error or invalid selection.
   */
  bool readSourceCapability(PdSelection selection, SourceCapability& outCapability);

  /*
   * requestProfile() — Ask the HUSB238 to negotiate the specified PD voltage
   * profile with the attached source.  Writes the selection to SRC_PDO_SELECT
   * (0x08) then issues the REQUEST_PD command (0x01) to GO_COMMAND (0x09).
   * The negotiation result is available in Status::response after the source
   * responds (typically within a few hundred milliseconds).
   * Returns true if the I2C writes succeeded; false on error.
   */
  bool requestProfile(PdSelection selection);

  /*
   * requestSourceCapabilities() — Issue a GET_SOURCE_CAPABILITIES PD message by
   * writing command 0x04 to GO_COMMAND (0x09).  This asks the source to
   * re-broadcast its full list of supported PDOs, refreshing the SRC_PDO registers
   * on the HUSB238.  Call readSourceCapability() afterwards to inspect the results.
   * Returns true if the I2C write succeeded; false on error.
   */
  bool requestSourceCapabilities();

  /*
   * hardReset() — Trigger a USB PD Hard Reset by writing command 0x10 to
   * GO_COMMAND (0x09).  Both the HUSB238 and the attached source re-enumerate
   * the PD connection from scratch.  Use sparingly; a hard reset briefly
   * interrupts VBUS power to the system load.
   * Returns true if the I2C write succeeded; false on error.
   */
  bool hardReset();

  /*
   * readRegister() — Read a single byte from the register at reg.
   * Exposed publicly to allow diagnostic access to any HUSB238 register without
   * going through the higher-level methods.
   */
  bool readRegister(uint8_t reg, uint8_t& outValue);

  /*
   * writeRegister() — Write a single byte to the register at reg.
   * Exposed publicly to allow direct register-level control where no higher-level
   * API method exists.
   */
  bool writeRegister(uint8_t reg, uint8_t value);

  /*
   * lastError() — Returns the most recent error code.
   */
  Error lastError() const;

  /*
   * lastErrorString() — Returns a snake_case string describing the current error.
   * The returned pointer is to a string literal; do not free it.
   */
  const char* lastErrorString() const;

 private:

  // ─── REGISTER MAP ────────────────────────────────────────────────────────────

  /* Status registers (read-only). */
  static constexpr uint8_t kRegPdStatus0    = 0x00; // Negotiated voltage[7:4], current[1:0]
  static constexpr uint8_t kRegPdStatus1    = 0x01; // Attach[7], CC2[6], response[4:2]

  /* Source PDO availability registers (read-only, one per voltage). */
  static constexpr uint8_t kRegSrcPdo5V     = 0x02; // 5 V PDO: present[7], current[4:0]
  static constexpr uint8_t kRegSrcPdo9V     = 0x03; // 9 V PDO: present[7], current[4:0]
  static constexpr uint8_t kRegSrcPdo12V    = 0x04; // 12 V PDO: present[7], current[4:0]
  static constexpr uint8_t kRegSrcPdo15V    = 0x05; // 15 V PDO: present[7], current[4:0]
  static constexpr uint8_t kRegSrcPdo18V    = 0x06; // 18 V PDO: present[7], current[4:0]
  static constexpr uint8_t kRegSrcPdo20V    = 0x07; // 20 V PDO: present[7], current[4:0]

  /* PD command control registers (write). */
  static constexpr uint8_t kRegSrcPdoSelect = 0x08; // Profile selection for REQUEST_PD command
  static constexpr uint8_t kRegGoCommand    = 0x09; // Write command byte here to execute PD action

  // ─── GO_COMMAND BYTE VALUES ───────────────────────────────────────────────────

  /* Command bytes written to GO_COMMAND to trigger PD protocol actions. */
  static constexpr uint8_t kCommandRequestPd             = 0x01; // Request the selected PDO profile
  static constexpr uint8_t kCommandGetSourceCapabilities = 0x04; // Request source capability advertisement
  static constexpr uint8_t kCommandHardReset             = 0x10; // Initiate a USB PD Hard Reset

  // ─── PRIVATE METHODS ─────────────────────────────────────────────────────────

  /* Map a PdSelection enum value to the corresponding SRC_PDO register address
   * (kRegSrcPdo5V … kRegSrcPdo20V) for capability reads. */
  uint8_t profileToCapabilityRegister(PdSelection selection) const;

  /* Write command to GO_COMMAND (0x09) to trigger the corresponding PD action. */
  bool writeGoCommand(uint8_t command);

  // ─── PRIVATE MEMBER VARIABLES ─────────────────────────────────────────────────

  TwoWire& wire_;           // Reference to the shared I2C bus.
  uint8_t address_;         // I2C device address (fixed 0x08 for HUSB238).
  Error lastError_;         // Most recently set error code.
  bool busInitialized_;     // True once wire_.begin() has completed successfully.
};
