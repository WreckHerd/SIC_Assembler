#include "sic_assembler.h"

// --- Helper Function ---

// Evaluates an operand for EQU or ORG
// Returns the value and sets isAbsolute flag
int evaluateOperand(string operand, int currentLoc, int currentBlockNum, bool& isAbsolute) {
    if (operand == "*") {
        isAbsolute = false; // Relocatable
        return currentLoc;
    }
    if (isNumber(operand)) {
        isAbsolute = true; // Absolute
        return stoi(operand);
    }
    if (SYMTAB.count(operand)) {
        // Value depends on the symbol it's referencing
        isAbsolute = SYMTAB[operand].isAbsolute;
        return SYMTAB[operand].address; // Return the value (relocatable addr or absolute val)
    }
    
    // Handle simple expressions (e.g., SYMBOL+NUMBER, SYMBOL+SYMBOL, SYMBOL-NUMBER)
    size_t plusPos = operand.find('+');
    size_t minusPos = operand.find('-');
    size_t opPos = (plusPos != string::npos) ? plusPos : minusPos;
    char opChar = (plusPos != string::npos) ? '+' : '-';

    if (opPos != string::npos) {
        string left = operand.substr(0, opPos);
        string right = operand.substr(opPos + 1);
        
        int leftVal = 0;
        int rightVal = 0;
        bool leftAbs = false;
        bool rightAbs = false;

        // Evaluate left side
        if (left == "*") {
            leftVal = currentLoc;
            leftAbs = false;
        } else if (isNumber(left)) {
            leftVal = stoi(left);
            leftAbs = true;
        } else if (SYMTAB.count(left)) {
            leftVal = SYMTAB[left].address;
            leftAbs = SYMTAB[left].isAbsolute;
        } else {
            throw runtime_error("Invalid expression (LHS): " + operand);
        }

        // Evaluate right side
        if (isNumber(right)) {
            rightVal = stoi(right);
            rightAbs = true;
        } else if (SYMTAB.count(right)) {
            rightVal = SYMTAB[right].address;
            rightAbs = SYMTAB[right].isAbsolute;
        } else {
            throw runtime_error("Invalid expression (RHS): " + operand);
        }

        // --- Determine type of result ---
        if (opChar == '+') {
            if (leftAbs && rightAbs) {
                isAbsolute = true;
                return leftVal + rightVal;
            } else if (!leftAbs && rightAbs) { // Relocatable + Absolute
                isAbsolute = false;
                return leftVal + rightVal;
            } else if (leftAbs && !rightAbs) { // Absolute + Relocatable
                isAbsolute = false;
                return leftVal + rightVal;
            } else { // Relocatable + Relocatable
                throw runtime_error("Invalid expression (Rel+Rel): " + operand);
            }
        } else { // opChar == '-'
            if (leftAbs && rightAbs) {
                isAbsolute = true;
                return leftVal - rightVal;
            } else if (!leftAbs && rightAbs) { // Relocatable - Absolute
                isAbsolute = false;
                return leftVal - rightVal;
            } else if (leftAbs && !rightAbs) { // Absolute - Relocatable
                throw runtime_error("Invalid expression (Abs-Rel): " + operand);
            } else { // Relocatable - Relocatable
                // This is only valid if they are in the same block
                if (SYMTAB[left].blockNumber == SYMTAB[right].blockNumber) {
                    isAbsolute = true; // Result is an absolute number
                    return leftVal - rightVal;
                } else {
                    throw runtime_error("Invalid expression (Rel-Rel from diff blocks): " + operand);
                }
            }
        }
    }

    // Fallback to plain hex (e.g., ORG 1000)
    try {
        isAbsolute = true; // Assume hex numbers are absolute
        return hexToInt(operand);
    } catch (...) {
        throw runtime_error("Invalid operand: " + operand);
    }
}


// Handles placing literals from LITTAB into the current block
void handle_ltorg(ofstream& intermediateFile) {
    for (auto it = LITTAB.begin(); it != LITTAB.end(); ++it) {
        if (it->second.address == -1) { // If literal address is not yet assigned
            string literal = it->first;
            string literalValue = it->second.value;
            int literalSize = it->second.size;

            // Assign address from the current block's LOCCTR
            it->second.address = BLOCKTAB[BLOCK_ID_TO_NAME[currentBlockNumber]].locctr;
            it->second.blockNumber = currentBlockNumber;

            // Write to intermediate file
            intermediateFile << intToHex(it->second.address, 4) << "\t"
                             << "*" << "\t" << literal << "\t" << "\t"
                             << currentBlockNumber << endl;

            // Increment current block's LOCCTR
            BLOCKTAB[BLOCK_ID_TO_NAME[currentBlockNumber]].locctr += literalSize;
        }
    }
}

// --- Pass 1 Implementation ---
void performPass1(const string& inputFilename, const string& intermediateFilename) {
    ifstream inputFile(inputFilename);
    ofstream intermediateFile(intermediateFilename);
    string line;

    if (!inputFile.is_open()) {
        throw runtime_error("Could not open input file: " + inputFilename);
    }
    if (!intermediateFile.is_open()) {
        throw runtime_error("Could not create intermediate file: " + intermediateFilename);
    }

    // Initialize default program block (Block 0)
    currentBlockNumber = 0;
    BLOCKTAB["DEFAULT"] = {0, 0, 0};
    BLOCK_ID_TO_NAME[0] = "DEFAULT";
    int blockCount = 1;

    string programName = "DEFAULT";
    int startAddress = 0;
    
    // Read first line to get START address
    getline(inputFile, line);
    stringstream ss(line);
    string label, opcode, operand;
    ss >> label >> opcode >> operand;

    if (opcode == "START") {
        programName = label;
        startAddress = hexToInt(operand);
        BLOCKTAB["DEFAULT"].locctr = startAddress;
        
        intermediateFile << intToHex(startAddress, 4) << "\t" << label << "\t" 
                         << opcode << "\t" << operand << "\t" << currentBlockNumber << endl;
    } else {
        // No START directive, rewind and process as normal line
        inputFile.seekg(0, ios::beg);
        BLOCKTAB["DEFAULT"].locctr = 0;
    }

    // --- Main Pass 1 Loop ---
    while (getline(inputFile, line)) {
        if (line.empty()) continue;
        
        size_t firstCharPos = line.find_first_not_of(" \t");
        if (firstCharPos == string::npos) continue; // Skip empty/whitespace-only lines
        
        if (line[firstCharPos] == '.') {
             if (!line.empty()) intermediateFile << "\t" << line << endl; // Pass comments
             continue;
        }

        ss.clear();
        ss.str(line);
        label = ""; opcode = ""; operand = ""; // Reset

        // ***** ROBUST PARSER *****
        string rest_of_line;
        // Check if line starts with whitespace (no label)
        if (line[0] == ' ' || line[0] == '\t') {
            label = "";
            ss >> opcode; 
        } else {
            // Has label
            ss >> label >> opcode;
        }
        
        // Read the rest of the line
        getline(ss, rest_of_line);
        
        // Trim leading space from rest_of_line
        rest_of_line.erase(0, rest_of_line.find_first_not_of(" \t"));
        
        // Find comment
        size_t commentPos = rest_of_line.find('.');
        if (commentPos != string::npos) {
            // Found a comment, check if it's inside quotes
            size_t quote1 = rest_of_line.find('\'');
            if (quote1 == string::npos || quote1 > commentPos) {
                 // No quotes, or comment is before quotes (so it's a real comment)
                 operand = rest_of_line.substr(0, commentPos);
            } else {
                // Comment is *after* first quote. Is it inside?
                size_t quote2 = rest_of_line.find('\'', quote1 + 1);
                if (quote2 != string::npos && commentPos > quote2) {
                    // Comment is after the closing quote, so it's a real comment
                    operand = rest_of_line.substr(0, commentPos);
                } else {
                    // Comment is inside quotes (e.g., BYTE C'...') or no closing quote
                    operand = rest_of_line;
                }
            }
        } else {
            // No comment
            operand = rest_of_line;
        }

        // Trim trailing whitespace from operand
        operand.erase(operand.find_last_not_of(" \t") + 1);
        // ***** END OF ROBUST PARSER *****
        
        string currentBlockName = BLOCK_ID_TO_NAME[currentBlockNumber];
        int currentLoc = BLOCKTAB[currentBlockName].locctr;

        // Write to intermediate file *before* changing locctr
        intermediateFile << intToHex(currentLoc, 4) << "\t"
                         << label << "\t" << opcode << "\t" << operand << "\t"
                         << currentBlockNumber << endl;
        
        // --- Symbol Table Logic ---
        if (!label.empty() && label != "*") {
            if (SYMTAB.count(label)) {
                throw runtime_error("Duplicate symbol: " + label);
            }
            // By default, symbols are relocatable
            SYMTAB[label] = {currentLoc, currentBlockNumber, false};
        }

        // --- Opcode Processing ---
        string opcode_to_check = opcode;
        bool isExtended = false;
        if (opcode_to_check[0] == '+') {
            opcode_to_check = opcode_to_check.substr(1);
            isExtended = true;
        }
        
        if (OPTAB.count(opcode_to_check)) {
            if (isExtended) {
                BLOCKTAB[currentBlockName].locctr += 4;
            } else {
                BLOCKTAB[currentBlockName].locctr += OPTAB[opcode_to_check].format;
            }
        } else if (opcode == "WORD") {
            BLOCKTAB[currentBlockName].locctr += 3;
        } else if (opcode == "RESW") {
            if (operand.empty()) throw runtime_error("Missing operand for RESW at loc " + intToHex(currentLoc,4));
            BLOCKTAB[currentBlockName].locctr += (3 * stoi(operand));
        } else if (opcode == "RESB") {
            if (operand.empty()) throw runtime_error("Missing operand for RESB at loc " + intToHex(currentLoc,4));
            BLOCKTAB[currentBlockName].locctr += stoi(operand);
        } else if (opcode == "BYTE") {
            if (operand[0] == 'X') { // Hex constant
                BLOCKTAB[currentBlockName].locctr += (operand.length() - 3) / 2;
            } else if (operand[0] == 'C') { // Character constant
                BLOCKTAB[currentBlockName].locctr += (operand.length() - 3);
            }
        } else if (opcode == "EQU") {
            if (label.empty()) { throw runtime_error("EQU must have a label"); }
            bool isAbs = true;
            int value = evaluateOperand(operand, currentLoc, currentBlockNumber, isAbs);
            // Overwrite symbol info
            SYMTAB[label] = {value, (isAbs ? -1 : currentBlockNumber), isAbs}; // Use -1 block for absolute
        } else if (opcode == "ORG") {
            bool isAbs = true;
            int newLoc = evaluateOperand(operand, currentLoc, currentBlockNumber, isAbs);
            BLOCKTAB[currentBlockName].locctr = newLoc;
        }
        else if (opcode == "USE") {
            if (operand.empty()) {
                currentBlockNumber = 0;
            } else {
                if (!BLOCKTAB.count(operand)) {
                    BLOCKTAB[operand] = {blockCount, 0, 0};
                    BLOCK_ID_TO_NAME[blockCount] = operand;
                    currentBlockNumber = blockCount;
                    blockCount++;
                } else {
                    currentBlockNumber = BLOCKTAB[operand].blockNumber;
                }
            }
        } else if (opcode == "LTORG") {
            handle_ltorg(intermediateFile);
        } else if (opcode == "END") {
            handle_ltorg(intermediateFile);
            break;
        } else if (opcode == "BASE" || opcode == "NOBASE") {
            // No locctr change
        } else if (opcode != "START") {
            throw runtime_error("Invalid opcode: " + opcode);
        }
        
        // --- Literal Table Logic ---
        string operand_for_literal_check = operand;
        // Handle "literal, X"
        size_t commaPos = operand.rfind(',');
        if (commaPos != string::npos) {
            string afterComma = operand.substr(commaPos + 1);
            // Trim whitespace from afterComma
            afterComma.erase(0, afterComma.find_first_not_of(" \t"));
            if (afterComma == "X") {
                operand_for_literal_check = operand.substr(0, commaPos);
                // Trim trailing whitespace from the literal part
                operand_for_literal_check.erase(operand_for_literal_check.find_last_not_of(" \t") + 1);
            }
        }


        if (operand_for_literal_check[0] == '=') {
            if (LITTAB.find(operand_for_literal_check) == LITTAB.end()) {
                string literal = operand_for_literal_check;
                string literalType = literal.substr(1,1);
                string literalValue = literal.substr(3, literal.length() - 4);
                int literalSize = 0;

                if (literalType == "X") {
                    literalSize = literalValue.length() / 2;
                    LITTAB[literal] = {-1, -1, literalSize, literalValue};
                } else if (literalType == "C") {
                    literalSize = literalValue.length();
                    LITTAB[literal] = {-1, -1, literalSize, stringToHex(literalValue)};
                } else {
                    throw runtime_error("Invalid literal format: " + literal);
                }
            }
        }
    }
    
    intermediateFile.close();
    inputFile.close();

    // --- Calculate Block Starting Addresses ---
    int currentStartAddress = startAddress;
    for (int i = 0; i < blockCount; ++i) {
        string blockName = BLOCK_ID_TO_NAME[i];
        BLOCKTAB[blockName].startAddress = currentStartAddress;
        currentStartAddress += BLOCKTAB[blockName].locctr;
    }

    programLength = currentStartAddress - startAddress;

    // --- Print SYMTAB and BLOCKTAB (for debugging) ---
    cout << "\n--- SYMBOL TABLE ---" << endl;
    cout << "Symbol\tAddr\tBlk\tAbs" << endl;
    cout << "--------------------------" << endl;
    for (auto const& [key, val] : SYMTAB) {
        cout << key << "\t" << intToHex(val.address, 4) << "\t" 
             << val.blockNumber << "\t" << (val.isAbsolute ? "Y" : "N") << endl;
    }
    
    cout << "\n--- LITERAL TABLE ---" << endl;
    cout << "Literal\tValue\tSize\tAddr\tBlk" << endl;
    cout << "-----------------------------------" << endl;
    for (auto const& [key, val] : LITTAB) {
        cout << key << "\t" << val.value << "\t" << val.size << "\t" 
             << intToHex(val.address, 4) << "\t" << val.blockNumber << endl;
    }

    cout << "\n--- BLOCK TABLE ---" << endl;
    cout << "Block\tNum\tStart\tLength" << endl;
    cout << "-----------------------------------" << endl;
    for (auto const& [key, val] : BLOCKTAB) {
        cout << key << "\t" << val.blockNumber << "\t" 
             << intToHex(val.startAddress, 4) << "\t"
             << intToHex(val.locctr, 4) << endl;
    }
    cout << "Total Program Length: " << intToHex(programLength, 6) << endl;
}
