#include "sic_assembler.h"

// --- Helper Functions ---

// Gets register number
int getRegisterNumber(const string& reg) {
    if (reg == "A") return 0;
    if (reg == "X") return 1;
    if (reg == "L") return 2;
    if (reg == "B") return 3;
    if (reg == "S") return 4;
    if (reg == "T") return 5;
    if (reg == "F") return 6;
    if (reg == "PC") return 8;
    if (reg == "SW") return 9;
    return -1;
}

// Parses registers for Format 2
string parseFormat2(const string& operand) {
    stringstream ss(operand);
    string reg1, reg2;
    getline(ss, reg1, ',');
    getline(ss, reg2);
    
    int r1 = getRegisterNumber(reg1);
    int r2 = getRegisterNumber(reg2);
    
    if (r2 == -1) r2 = 0; // Handle single register ops like CLEAR X

    return intToHex(r1, 1) + intToHex(r2, 1);
}

// Calculates Target Address and generates object code
string generateObjectCode(const string& opcode, const string& operand, int loc, int blockNum, bool isExtended, string& error) {
    
    // --- Handle Assembler Directives ---
    if (opcode == "BYTE") {
        if (operand[0] == 'X') { // Hex
            return operand.substr(2, operand.length() - 3);
        } else if (operand[0] == 'C') { // Char
            return stringToHex(operand.substr(2, operand.length() - 3));
        }
    }
    if (opcode == "WORD") {
        // Can be a number or a relocatable symbol
        if (isNumber(operand)) {
            return intToHex(stoi(operand), 6);
        } else if (SYMTAB.count(operand)) {
            // Handle WORD CEND-CSTART (not implemented)
            // For now, assume WORD with a single symbol
            return intToHex(SYMTAB[operand].address, 6);
        }
        return "000000";
    }
    // Handle literal definition (e.g., from LTORG)
    if (opcode[0] == '=') {
        string literalType = opcode.substr(1,1);
        string literalValue = opcode.substr(3, opcode.length() - 4);
        if(literalType == "C") {
            return stringToHex(literalValue);
        } else { // Hex
            return literalValue;
        }
    }
    if (opcode == "RESW" || opcode == "RESB" || opcode == "START" || opcode == "END" || opcode == "USE" || opcode == "BASE" || opcode == "NOBASE" || opcode == "LTORG" || opcode == "EQU" || opcode == "ORG") {
        return ""; // No object code
    }

    // --- Handle Instructions ---
    if (!OPTAB.count(opcode)) {
        throw runtime_error("Invalid opcode in Pass 2: " + opcode);
    }

    OpInfo op = OPTAB[opcode];
    string objCode = "";

    // --- Format 1 ---
    if (op.format == 1) {
        return op.opcode;
    }

    // --- Format 2 ---
    if (op.format == 2) {
        return op.opcode + parseFormat2(operand);
    }

    // --- Format 3/4 ---
    if (op.format == 3) {
        int n = 1, i = 1, x = 0, b = 0, p = 0, e = 0;
        int disp = 0;
        int targetAddress = 0;
        string cleanOperand = operand;

        // Set n/i flags
        if (operand.empty()) { // RSUB
            n = 1; i = 1;
        } else if (operand[0] == '#') { // Immediate
            n = 0; i = 1;
            cleanOperand = operand.substr(1);
        } else if (operand[0] == '@') { // Indirect
            n = 1; i = 0;
            cleanOperand = operand.substr(1);
        }
        
        // ***** FIX FOR INDEXED OPERANDS WITH SPACES *****
        // Remove all whitespace from cleanOperand
        cleanOperand.erase(remove(cleanOperand.begin(), cleanOperand.end(), ' '), cleanOperand.end());
        cleanOperand.erase(remove(cleanOperand.begin(), cleanOperand.end(), '\t'), cleanOperand.end());
        
        // Set x flag (Indexed)
        size_t commaPos = cleanOperand.rfind(",X");
        if (commaPos != string::npos && commaPos == cleanOperand.length() - 2) {
            x = 1;
            cleanOperand = cleanOperand.substr(0, commaPos);
        }
        // ***** END OF FIX *****
        
        // Set e flag (Format 4)
        if (isExtended) {
            e = 1;
        }

        // --- Calculate Target Address and Displacement ---
        if (operand.empty()) { // RSUB
            targetAddress = 0; disp = 0; p = 0; b = 0;
        }
        else if (isNumber(cleanOperand)) { // Immediate numeric value (e.g., LDA #10)
            targetAddress = stoi(cleanOperand);
            disp = targetAddress;
            p = 0; b = 0; // Not PC/BASE relative
        } 
        else if (SYMTAB.count(cleanOperand)) {
            SymbolInfo sym = SYMTAB[cleanOperand];
            if (sym.isAbsolute) {
                // Absolute symbol (from EQU)
                targetAddress = sym.address;
                disp = targetAddress;
                p = 0; b = 0; // Absolute value, not PC/BASE relative
            } else {
                // Relocatable symbol
                targetAddress = sym.address + BLOCKTAB[BLOCK_ID_TO_NAME[sym.blockNumber]].startAddress;
                
                if (e == 1) { // Format 4
                    disp = targetAddress;
                    p = 0; b = 0;
                } else { // Format 3 - PC or BASE
                    int pc = loc + BLOCKTAB[BLOCK_ID_TO_NAME[blockNum]].startAddress + 3;
                    disp = targetAddress - pc;
                    if (disp >= -2048 && disp <= 2047) {
                        p = 1; b = 0;
                        if (disp < 0) disp += 4096; // 12-bit 2's complement
                    } else if (baseRegisterAvailable) {
                        disp = targetAddress - hexToInt(baseRegisterValue);
                        if (disp >= 0 && disp <= 4095) {
                            p = 0; b = 1;
                        } else {
                            error = "Displacement out of range (PC and BASE)";
                            disp = 0;
                        }
                    } else {
                        error = "Displacement out of range (PC), no BASE";
                        disp = 0;
                    }
                }
            }
        } 
        else if (LITTAB.count(cleanOperand)) {
            // Literal (always relocatable)
            LiteralInfo lit = LITTAB[cleanOperand];
            targetAddress = lit.address + BLOCKTAB[BLOCK_ID_TO_NAME[lit.blockNumber]].startAddress;

            if (e == 1) { // Format 4
                disp = targetAddress;
                p = 0; b = 0;
            } else { // Format 3 - PC or BASE
                int pc = loc + BLOCKTAB[BLOCK_ID_TO_NAME[blockNum]].startAddress + 3;
                disp = targetAddress - pc;
                if (disp >= -2048 && disp <= 2047) {
                    p = 1; b = 0;
                    if (disp < 0) disp += 4096; // 12-bit 2's complement
                } else if (baseRegisterAvailable) {
                    disp = targetAddress - hexToInt(baseRegisterValue);
                    if (disp >= 0 && disp <= 4095) {
                        p = 0; b = 1;
                    } else {
                        error = "Displacement out of range (PC and BASE)";
                        disp = 0;
                    }
                } else {
                    error = "Displacement out of range (PC), no BASE";
                    disp = 0;
                }
            }
        }
        else {
            error = "Undefined symbol '" + cleanOperand + "'";
            targetAddress = 0; disp = 0;
        }

        // --- Assemble Object Code ---
        int opcodeInt = hexToInt(op.opcode);
        
        // Format 3: op(6) ni(2) xbpe(4) disp(12)
        if (e == 0) {
            int flags = (n << 1) | i;
            objCode = intToHex(opcodeInt + flags, 2); // 8 bits
            
            int xbpe_flags = (x << 3) | (b << 2) | (p << 1) | e;
            objCode += intToHex(xbpe_flags, 1); // 4 bits
            
            objCode += intToHex(disp & 0xFFF, 3); // 12 bits (mask for safety)
        } 
        // Format 4: op(6) ni(2) xbpe(4) address(20)
        else {
            int flags = (n << 1) | i;
            objCode = intToHex(opcodeInt + flags, 2); // 8 bits

            int xbpe_flags = (x << 3) | (b << 2) | (p << 1) | e;
            objCode += intToHex(xbpe_flags, 1); // 4 bits

            objCode += intToHex(disp & 0xFFFFF, 5); // 20 bits (mask for safety)
        }
        return objCode;
    }

    return "";
}


// --- Pass 2 Implementation ---
void performPass2(const string& intermediateFilename, const string& listingFilename, const string& objectFilename, const string& startAddressStr) {
    ifstream intermediateFile(intermediateFilename);
    ofstream listingFile(listingFilename);
    ofstream objectFile(objectFilename);

    if (!intermediateFile.is_open()) {
        throw runtime_error("Could not open intermediate file: " + intermediateFilename);
    }
    if (!listingFile.is_open()) {
        throw runtime_error("Could not create listing file: " + listingFilename);
    }
    if (!objectFile.is_open()) {
        throw runtime_error("Could not create object file: "s + objectFilename);
    }

    string line;
    string programName = "PROG";
    int startAddress = hexToInt(startAddressStr);

    string textRecord = "";
    string currentTextRecordStartAddr = "";
    int currentBlock = 0;
    map<int, string> textRecords; // Map of block number to its current text record
    map<int, string> textRecordStarts; // Map of block number to its T-record start address

    // --- Header Record ---
    getline(intermediateFile, line); // Read first line (START)
    stringstream ss(line);
    string loc, label, opcode, operand, block;
    
    string part;
    vector<string> parts;
    while (getline(ss, part, '\t')) {
        parts.push_back(part);
    }

    // Expect 5 parts: LOC \t LABEL \t START \t OPERAND \t BLOCK
    if (parts.size() == 5 && parts[2] == "START") {
        loc = parts[0];
        label = parts[1];
        opcode = parts[2];
        operand = parts[3];
        block = parts[4];
        programName = label;
    }
    
    // Write Header record
    objectFile << "H" << left << setfill(' ') << setw(6) << programName
               << intToHex(startAddress, 6) << intToHex(programLength, 6) << endl;

    // Reset file to read from the beginning
    intermediateFile.seekg(0, ios::beg);

    // --- Main Pass 2 Loop ---
    while (getline(intermediateFile, line)) {
        if (line.empty()) continue;

        // Handle comments
        size_t firstCharPos = line.find_first_not_of(" \t");
        if (firstCharPos == string::npos) continue; // Whitespace only
        
        if (line[firstCharPos] == '.') {
            listingFile << line << endl;
            continue;
        }

        ss.clear();
        ss.str(line);
        loc = ""; label = ""; opcode = ""; operand = ""; block = "";
        
        // --- Robust parsing
        parts.clear();
        while (getline(ss, part, '\t')) {
            parts.push_back(part);
        }

        if (parts.size() == 5) {
            loc = parts[0];
            label = parts[1];
            opcode = parts[2];
            operand = parts[3]; // This operand is now clean (no comments)
            block = parts[4];
        } else {
            cerr << "Warning: Skipping malformed intermediate line: " << line << endl;
            continue;
        }
        
        
        // Handle literal definitions
        if(label == "*") {
            opcode = parts[2]; // Opcode is the literal itself
            operand = "";      // No operand
        }
        
        // Handle format 4
        bool isExtended = false;
        string originalOpcode = opcode; // Save for listing
        if(opcode[0] == '+') {
            isExtended = true;
            opcode = opcode.substr(1);
        }

        // Get current block
        if (!block.empty()) {
            currentBlock = stoi(block);
        } else {
            currentBlock = 0; // Failsafe
        }

        // Calculate absolute address for listing
        int absoluteLoc = 0;
        if (!loc.empty()) {
             absoluteLoc = hexToInt(loc) + BLOCKTAB[BLOCK_ID_TO_NAME[currentBlock]].startAddress;
        }

        // --- Handle Directives ---
        if (opcode == "START") {
            listingFile << loc << "\t" << label << "\t" << opcode << "\t" << operand << endl;
            continue;
        }
        
        if (opcode == "USE") {
            // Write out the old text record
            if(!textRecords[currentBlock].empty()) {
                objectFile << "T" << intToHex(hexToInt(textRecordStarts[currentBlock]), 6)
                       << intToHex(textRecords[currentBlock].length() / 2, 2)
                       << textRecords[currentBlock] << endl;
            }
            textRecords[currentBlock] = "";
            textRecordStarts[currentBlock] = "";
            
            // Switch current block
            currentBlock = operand.empty() ? 0 : BLOCKTAB[operand].blockNumber;
            listingFile << loc << "\t" << label << "\t" << originalOpcode << "\t" << operand << endl;
            continue;
        }

        if (opcode == "BASE") {
            baseRegisterAvailable = true;
            baseRegisterValue = getOperandValue(operand); // Get absolute address
            listingFile << loc << "\t" << label << "\t" << opcode << "\t" << operand << endl;
            continue;
        }
        
        if (opcode == "NOBASE" || opcode == "EQU" || opcode == "ORG") {
            listingFile << loc << "\t" << label << "\t" << opcode << "\t" << operand << endl;
            continue; // No object code
        }
        
        if (opcode == "LTORG") {
             listingFile << loc << "\t" << label << "\t" << opcode << "\t" << operand << endl;
             continue; // No object code
        }

        if (opcode == "END") {
            // Write out all remaining text records
            for(auto const& [blockNum, rec] : textRecords) {
                if(!rec.empty()) {
                    objectFile << "T" << intToHex(hexToInt(textRecordStarts[blockNum]), 6)
                           << intToHex(rec.length() / 2, 2)
                           << rec << endl;
                }
            }
            listingFile << loc << "\t" << label << "\t" << opcode << "\t" << operand << endl;
            objectFile << "E" << intToHex(startAddress, 6) << endl; // End record
            break;
        }

        // --- Generate Object Code ---
        string error = "";
        string objCode = generateObjectCode(opcode, operand, hexToInt(loc), currentBlock, isExtended, error);

        // --- Listing File Output ---
        string locStr = loc;
        if (opcode == "USE" || opcode == "LTORG" || opcode == "ORG") {
            locStr = ""; // Directives don't have an address in the same way
        } else {
            locStr = intToHex(absoluteLoc, 4);
        }

        listingFile << left << setw(6) << locStr
                    << setw(8) << label
                    << setw(8) << originalOpcode // Use original opcode (+LDA)
                    << setw(12) << operand      // Use original operand (with spaces)
                    << setw(12) << objCode
                    << "  " << error << endl;

        // --- Object File (Text Record) Logic ---
        if (!objCode.empty() && error.empty()) {
            
            if (textRecords[currentBlock].empty()) {
                textRecordStarts[currentBlock] = intToHex(absoluteLoc, 6);
            }
            
            if (textRecords[currentBlock].length() + objCode.length() > 60) {
                // Write current record
                objectFile << "T" << intToHex(hexToInt(textRecordStarts[currentBlock]), 6)
                           << intToHex(textRecords[currentBlock].length() / 2, 2)
                           << textRecords[currentBlock] << endl;
                // Start new record
                textRecords[currentBlock] = objCode;
                textRecordStarts[currentBlock] = intToHex(absoluteLoc, 6);
            } else {
                // Append to current record
                textRecords[currentBlock] += objCode;
            }
        } else if ((opcode == "RESW" || opcode == "RESB") && !textRecords[currentBlock].empty()) {
            // End current text record
             objectFile << "T" << intToHex(hexToInt(textRecordStarts[currentBlock]), 6)
                       << intToHex(textRecords[currentBlock].length() / 2, 2)
                       << textRecords[currentBlock] << endl;
            textRecords[currentBlock] = "";
            textRecordStarts[currentBlock] = "";
        }
    }

    intermediateFile.close();
    listingFile.close();
    objectFile.close();
}
