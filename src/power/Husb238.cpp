/*
 * Husb238.cpp — Implementation of the HUSB238 USB Power Delivery sink driver.
 *
 * Provides all hardware interaction for the Hynetek HUSB238 USB Type-C PD sink
 * controller IC.
 *
 * ── I2C Protocol and Address ─────────────────────────────────────────────────
 * The HUSB238 communicates over a standard I2C bus at the fixed 7-bit address
 * 0x08.  The bus operates in Fast Mode (400 kHz).  All registers are 8-bit
 * wide and accessed with single-byte read and write transactions:
 *   Read:  [START | addr+W | reg | RS | addr+R | byte | STOP]
 *   Write: [START | addr+W | reg | byte | STOP]
 *
 * Unlike the BQ25628E and BQ27441 drivers, this driver does not use multi-byte
 * register accesses and does not perform automatic I2C bus restarts on failure.
 * All reads and writes are single-shot — no retry loop is implemented.
 *
 * ── GO_COMMAND Register ──────────────────────────────────────────────────────
 * PD protocol actions are triggered by writing a command byte to the GO_COMMAND
 * register (0x09).  The upper 3 bits of this register are reserved and must be
 * preserved; only bits [4:0] carry the command.  writeGoCommand() performs a
 * read-modify-write to preserve the reserved bits:
 *   1. Read the current GO_COMMAND value.
 *   2. Clear bits [4:0]: goCommand &= 0xE0.
 *   3. OR in the command byte (masked to 5 bits): goCommand |= (cmd & 0x1F).
 *   4. Write the modified byte back to 0x09.
 *
 * Supported commands (bits [4:0]):
 *   0x01 — REQUEST_PD              : negotiate the PDO selected in SRC_PDO_SELECT.
 *   0x04 — GET_SOURCE_CAPABILITIES : ask the source to re-broadcast its PDO list.
 *   0x10 — HARD_RESET              : initiate a USB PD Hard Reset sequence.
 *
 * ── PD Profile Selection ─────────────────────────────────────────────────────
 * To request a specific voltage PDO from the attached source:
 *   1. Read SRC_PDO_SELECT (0x08) to preserve any reserved bits [3:0].
 *   2. Place the PdSelection enum value in bits [7:4] of SRC_PDO_SELECT.
 *   3. Write back to 0x08.
 *   4. Issue the REQUEST_PD command (0x01) to GO_COMMAND (0x09).
 * The HUSB238 then sends the PD "Request" message autonomously; the result is
 * reflected in Status::response (PD_STATUS1 bits [4:2]) after the source responds.
 *
 * ── Source Capability Registers ──────────────────────────────────────────────
 * Registers 0x02–0x07 each describe one fixed-voltage PDO advertised by the
 * attached source.  Each byte encodes:
 *   Bit  7    : present — 1 if the source advertises this voltage, 0 otherwise.
 *   Bits [4:0]: current code — maximum current for this PDO (per datasheet Table 5).
 * These registers are populated when GET_SOURCE_CAPABILITIES is issued (or
 * automatically on attach) and are stale until the source re-advertises them.
 *
 * Interface: include/power/Husb238.hpp
 */

#include "power/Husb238.hpp"

// ─── CONSTRUCTOR ──────────────────────────────────────────────────────────────

/*
 * Stores references to the I2C bus and the device address, and initialises
 * driver state.  All hardware initialisation is deferred to begin().
 */
Husb238::Husb238(TwoWire& wire, uint8_t address)
    : wire_(wire),
      address_(address),
      lastError_(Error::None),
      busInitialized_(false) {}

// ─── LIFECYCLE ────────────────────────────────────────────────────────────────

/*
 * begin()
 *
 * Starts the I2C bus on the specified pins and clock frequency, then probes
 * the HUSB238 to confirm it is reachable.
 *
 * The bus is started only once (guarded by busInitialized_) to avoid
 * re-initialising a shared bus that may already be in use by another driver.
 *
 * Parameters:
 *   sdaPin      — GPIO pin number for I2C SDA.
 *   sclPin      — GPIO pin number for I2C SCL.
 *   frequencyHz — I2C clock frequency in Hz (typically 400000).
 *
 * Returns true if the bus started and the HUSB238 acknowledged the probe;
 * false if probe() fails (lastError_ is set to DeviceNotFound).
 */
bool Husb238::begin(int sdaPin, int sclPin, uint32_t frequencyHz) {
  if (!busInitialized_) {
    wire_.begin(sdaPin, sclPin, frequencyHz);
    busInitialized_ = true;
  }
  return probe();
}

/*
 * probe()
 *
 * Sends a zero-byte write transaction to address_ and checks for an ACK.
 * This is the standard I2C "address probe" pattern: no registers are read
 * or modified.
 *
 * Useful for detecting hot-plug events, verifying the bus after an error
 * recovery, or confirming the HUSB238 is powered before issuing commands.
 *
 * Returns true if the HUSB238 acknowledges the address; false otherwise.
 * Sets lastError_ to DeviceNotFound on NAK, Error::None on ACK.
 */
bool Husb238::probe() {
  wire_.beginTransmission(address_);
  const uint8_t error = wire_.endTransmission();
  if (error != 0) {
    lastError_ = Error::DeviceNotFound;
    return false;
  }
  lastError_ = Error::None;
  return true;
}

// ─── STATUS ───────────────────────────────────────────────────────────────────

/*
 * refreshStatus()
 *
 * Reads PD_STATUS0 (0x00) and PD_STATUS1 (0x01) and decodes the current PD
 * connection state into outStatus.
 *
 * PD_STATUS0 (register 0x00) bit field layout:
 *   Bits [7:4] — Negotiated VBUS voltage (VoltageCode enum):
 *                0b0000=Unattached, 0b0001=5V, 0b0010=9V, 0b0011=12V,
 *                0b0100=15V, 0b0101=18V, 0b0110=20V.
 *   Bits [3:0] — Negotiated current code (HUSB238 datasheet Table 4):
 *                Lower nibble; 0=0.9A, 1=1.5A, 2=2.0A, 3=3.0A, etc.
 *                Note: only the lower 4 bits are used; bits [3:0] map to currentCode.
 *
 * PD_STATUS1 (register 0x01) bit field layout:
 *   Bit  7     — CC2: 1 if CC2 is the active CC line (cable flipped).
 *   Bit  6     — ATTACH: 1 if a USB Type-C source is attached.
 *   Bits [4:2] — GO_COMMAND response code (ResponseCode enum):
 *                0b000=NoResponse, 0b001=Success, 0b011=InvalidCmd,
 *                0b100=NotSupported, 0b101=NoGoodCrc.
 *
 * Parameters:
 *   outStatus — filled with decoded values on success; partially populated on failure.
 *
 * Returns true on success; false if either register read fails.
 * Sets lastError_ on failure.
 */
bool Husb238::refreshStatus(Status& outStatus) {
  uint8_t status0 = 0;
  uint8_t status1 = 0;
  if (!readRegister(kRegPdStatus0, status0)) {
    return false;
  }
  if (!readRegister(kRegPdStatus1, status1)) {
    return false;
  }

  // Preserve raw bytes for diagnostics.
  outStatus.rawStatus0 = status0;
  outStatus.rawStatus1 = status1;

  // ── PD_STATUS1 (0x01) bit field decoding ─────────────────────────────────
  // Bit 6: ATTACH — source is attached and CC handshake completed.
  outStatus.attached = (status1 & (1U << 6)) != 0;
  // Bit 7: CC2 — set when the cable is oriented with CC2 as the active line.
  //   When false, CC1 is active.  Determines VCONN polarity for accessory mode.
  outStatus.cc2Connected = (status1 & (1U << 7)) != 0;
  // Bits [4:2]: response code from the last GO_COMMAND action.
  outStatus.response = static_cast<ResponseCode>((status1 >> 3) & 0x07);

  // ── PD_STATUS0 (0x00) bit field decoding ─────────────────────────────────
  // Bits [3:0]: current capability code for the negotiated PDO.
  outStatus.currentCode = (status0 & 0x0F);
  // Bits [7:4]: negotiated voltage code.
  outStatus.voltage = static_cast<VoltageCode>((status0 >> 4) & 0x0F);

  lastError_ = Error::None;
  return true;
}

// ─── SOURCE CAPABILITY QUERIES ────────────────────────────────────────────────

/*
 * readSourceCapability()
 *
 * Reads the per-voltage SRC_PDO register for the given PdSelection and decodes
 * it into a SourceCapability struct.
 *
 * SRC_PDO register byte layout (registers 0x02–0x07):
 *   Bit  7    : present — 1 if the attached source advertises this voltage PDO.
 *   Bits [6:5]: reserved.
 *   Bits [4:0]: current code for this PDO (same table as Status::currentCode).
 *
 * Parameters:
 *   selection      — the voltage PDO to query (must not be PD_NOT_SELECTED).
 *   outCapability  — filled with decoded PDO data on success.
 *
 * Returns true on success; false if selection is invalid or the I2C read fails.
 * Sets lastError_ to InvalidArgument for PD_NOT_SELECTED or an unmapped selection.
 */
bool Husb238::readSourceCapability(PdSelection selection,
                                   SourceCapability& outCapability) {
  // Map the PdSelection enum to the appropriate SRC_PDO register address.
  const uint8_t reg = profileToCapabilityRegister(selection);
  if (reg == 0xFF) {
    // 0xFF is the sentinel returned by profileToCapabilityRegister for invalid inputs.
    lastError_ = Error::InvalidArgument;
    return false;
  }

  uint8_t value = 0;
  if (!readRegister(reg, value)) {
    return false;
  }

  outCapability.raw = value;
  // Bit 7: present flag — indicates the source advertises this voltage.
  outCapability.present = ((value >> 7) & 0x01U) != 0;
  // Bits [4:0]: maximum current code for this PDO.
  outCapability.currentCode = value & 0x0FU;
  lastError_ = Error::None;
  return true;
}

// ─── PD COMMAND ACTIONS ───────────────────────────────────────────────────────

/*
 * requestProfile()
 *
 * Asks the HUSB238 to negotiate a specific PD voltage PDO with the attached
 * source.
 *
 * Steps:
 *   1. Read SRC_PDO_SELECT (0x08) to preserve its reserved lower nibble (bits [3:0]).
 *   2. Write the PdSelection enum value into bits [7:4], keeping bits [3:0] intact.
 *   3. Write the updated byte back to SRC_PDO_SELECT (0x08).
 *   4. Issue the REQUEST_PD command (0x01) via writeGoCommand() to trigger
 *      the PD negotiation.
 *
 * The HUSB238 handles the PD protocol autonomously after the command is issued.
 * The negotiation outcome is reflected in Status::response (PD_STATUS1 bits [4:2])
 * after the source sends its response (typically within a few hundred milliseconds).
 *
 * Parameters:
 *   selection — the voltage PDO to request; must not be PD_NOT_SELECTED.
 *
 * Returns true if the I2C writes succeeded; false on error or invalid selection.
 * Sets lastError_ to InvalidArgument if PD_NOT_SELECTED is passed.
 */
bool Husb238::requestProfile(PdSelection selection) {
  if (selection == PdSelection::PD_NOT_SELECTED) {
    lastError_ = Error::InvalidArgument;
    return false;
  }

  // Read the current SRC_PDO_SELECT value to preserve the reserved lower nibble.
  uint8_t srcPdo = 0;
  if (!readRegister(kRegSrcPdoSelect, srcPdo)) {
    return false;
  }

  // Clear bits [7:4] (the selection field), then OR in the new selection value.
  srcPdo &= 0x0F;  // Preserve reserved bits [3:0].
  srcPdo |= static_cast<uint8_t>(selection) << 4;  // Place selection in bits [7:4].
  if (!writeRegister(kRegSrcPdoSelect, srcPdo)) {
    return false;
  }

  // Issue the REQUEST_PD command to trigger the PD negotiation sequence.
  return writeGoCommand(kCommandRequestPd);
}

/*
 * requestSourceCapabilities()
 *
 * Sends a GET_SOURCE_CAPABILITIES PD control message by writing command 0x04
 * to the GO_COMMAND register (0x09).
 *
 * This message requests the source to broadcast its complete list of supported
 * PDOs (fixed voltage and current levels).  The HUSB238 updates the SRC_PDO
 * registers (0x02–0x07) when the source responds with its capabilities.
 * Call readSourceCapability() after this command completes to inspect the updated
 * PDO table.
 *
 * Returns true if the I2C write succeeded; false on error.
 */
bool Husb238::requestSourceCapabilities() {
  return writeGoCommand(kCommandGetSourceCapabilities);
}

/*
 * hardReset()
 *
 * Triggers a USB PD Hard Reset by writing command 0x10 to GO_COMMAND (0x09).
 *
 * A Hard Reset causes both the HUSB238 and the attached PD source to re-run
 * the full PD attach and capability discovery sequence from scratch.  After
 * the reset completes, VBUS will briefly drop and then return to 5 V until
 * a new PDO is negotiated.
 *
 * Use sparingly: a Hard Reset briefly interrupts VBUS power delivery to the
 * system load, which may cause downstream brownouts.  Intended for error
 * recovery when the PD session is in an unrecoverable state.
 *
 * Returns true if the I2C write succeeded; false on error.
 */
bool Husb238::hardReset() { return writeGoCommand(kCommandHardReset); }

// ─── REGISTER I/O PRIMITIVES ──────────────────────────────────────────────────

/*
 * readRegister()
 *
 * Reads a single byte from register reg using a repeated-START I2C transaction:
 *   [START | addr+W | reg | RS | addr+R | byte | STOP]
 *
 * Unlike the BQ25628E and BQ27441 drivers, this function does not implement a
 * retry loop.  A single failure sets lastError_ and returns false.
 *
 * Parameters:
 *   reg      — 8-bit register address.
 *   outValue — set to the byte read on success; unchanged on failure.
 *
 * Returns true on success; false on I2C write or read failure.
 * Sets lastError_ to I2cWriteFailed or I2cReadFailed on failure.
 */
bool Husb238::readRegister(uint8_t reg, uint8_t& outValue) {
  wire_.beginTransmission(address_);
  wire_.write(reg);
  // Repeated START (stop=false) holds the bus while the direction is reversed.
  if (wire_.endTransmission(false) != 0) {
    lastError_ = Error::I2cWriteFailed;
    return false;
  }

  const uint8_t bytesRequested = 1;
  const uint8_t bytesReceived = wire_.requestFrom(address_, bytesRequested);
  if (bytesReceived != bytesRequested || wire_.available() < 1) {
    lastError_ = Error::I2cReadFailed;
    return false;
  }

  outValue = static_cast<uint8_t>(wire_.read());
  lastError_ = Error::None;
  return true;
}

/*
 * writeRegister()
 *
 * Writes a single byte value to register reg in one I2C transaction:
 *   [START | addr+W | reg | value | STOP]
 *
 * No retry is implemented; a single failure sets lastError_ and returns false.
 *
 * Parameters:
 *   reg   — 8-bit register address.
 *   value — byte to write.
 *
 * Returns true on success; false on I2C write failure.
 * Sets lastError_ to I2cWriteFailed on failure.
 */
bool Husb238::writeRegister(uint8_t reg, uint8_t value) {
  wire_.beginTransmission(address_);
  wire_.write(reg);
  wire_.write(value);
  if (wire_.endTransmission() != 0) {
    lastError_ = Error::I2cWriteFailed;
    return false;
  }

  lastError_ = Error::None;
  return true;
}

// ─── STATUS ACCESSORS ─────────────────────────────────────────────────────────

/* Returns the most recently recorded error code. */
Husb238::Error Husb238::lastError() const { return lastError_; }

/*
 * lastErrorString()
 *
 * Returns a null-terminated snake_case C-string describing the current error.
 * The pointer refers to a string literal in flash; do not free it.
 */
const char* Husb238::lastErrorString() const {
  switch (lastError_) {
    case Error::None:
      return "none";
    case Error::DeviceNotFound:
      return "device_not_found";
    case Error::I2cWriteFailed:
      return "i2c_write_failed";
    case Error::I2cReadFailed:
      return "i2c_read_failed";
    case Error::InvalidArgument:
      return "invalid_argument";
    default:
      return "unknown";
  }
}

// ─── PRIVATE HELPERS ──────────────────────────────────────────────────────────

/*
 * profileToCapabilityRegister()
 *
 * Maps a PdSelection enum value to the corresponding SRC_PDO register address.
 * Used by readSourceCapability() to identify which register to read.
 *
 * Register map:
 *   PD_SRC_5V  → kRegSrcPdo5V  (0x02)
 *   PD_SRC_9V  → kRegSrcPdo9V  (0x03)
 *   PD_SRC_12V → kRegSrcPdo12V (0x04)
 *   PD_SRC_15V → kRegSrcPdo15V (0x05)
 *   PD_SRC_18V → kRegSrcPdo18V (0x06)
 *   PD_SRC_20V → kRegSrcPdo20V (0x07)
 *
 * Returns 0xFF as a sentinel for any invalid or unmapped selection value;
 * the caller must check for this before using the result.
 */
uint8_t Husb238::profileToCapabilityRegister(PdSelection selection) const {
  switch (selection) {
    case PdSelection::PD_SRC_5V:
      return kRegSrcPdo5V;
    case PdSelection::PD_SRC_9V:
      return kRegSrcPdo9V;
    case PdSelection::PD_SRC_12V:
      return kRegSrcPdo12V;
    case PdSelection::PD_SRC_15V:
      return kRegSrcPdo15V;
    case PdSelection::PD_SRC_18V:
      return kRegSrcPdo18V;
    case PdSelection::PD_SRC_20V:
      return kRegSrcPdo20V;
    default:
      return 0xFF;  // Sentinel: invalid or unmapped selection.
  }
}

/*
 * writeGoCommand()
 *
 * Performs a read-modify-write on the GO_COMMAND register (0x09) to issue a
 * PD protocol command while preserving the reserved upper bits [7:5].
 *
 * GO_COMMAND register layout:
 *   Bits [7:5] — reserved; must be preserved when writing.
 *   Bits [4:0] — command code; written to trigger a PD action.
 *
 * Algorithm:
 *   1. Read the current GO_COMMAND value (to preserve the reserved bits).
 *   2. Clear bits [4:0]: goCommand &= 0xE0  (0xE0 = 0b1110_0000).
 *   3. OR in the new command masked to 5 bits: goCommand |= (command & 0x1F).
 *   4. Write the updated byte back to GO_COMMAND.
 *
 * Parameters:
 *   command — 5-bit command code (see kCommandRequestPd, kCommandGetSourceCapabilities,
 *             kCommandHardReset).
 *
 * Returns true if both the read and write succeed; false on any I2C error.
 */
bool Husb238::writeGoCommand(uint8_t command) {
  // Read first to preserve the reserved upper bits.
  uint8_t goCommand = 0;
  if (!readRegister(kRegGoCommand, goCommand)) {
    return false;
  }

  // Clear the command field (bits [4:0]) then insert the new command.
  goCommand &= 0xE0;              // Preserve bits [7:5]; clear bits [4:0].
  goCommand |= (command & 0x1F);  // OR in the 5-bit command code.
  return writeRegister(kRegGoCommand, goCommand);
}
