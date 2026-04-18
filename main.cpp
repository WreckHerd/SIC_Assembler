#include "sic_assembler.h"

// --- Global Table Definitions ---
map<string, OpInfo> OPTAB;
map<string, SymbolInfo> SYMTAB;
map<string, BlockInfo> BLOCKTAB;
map<string, LiteralInfo> LITTAB;
map<int, string> BLOCK_ID_TO_NAME;

// --- Global Variable Definitions ---
int programLength = 0;
int currentBlockNumber = 0;
string baseRegisterValue = "0";
bool baseRegisterAvailable = false;

// --- Pass 1 and Pass 2 Function Declarations ---
void performPass1(const string& inputFilename, const string& intermediateFilename);
void performPass2(const string& intermediateFilename, const string& listingFilename, const string& objectFilename, const string& startAddress);

// --- Utility Function Implementations ---

// Converts an integer to a hexadecimal string
string intToHex(int n, int width) {
    stringstream ss;
    ss << hex << uppercase << setfill('0') << setw(width) << n;
    return ss.str();
}

// Converts a *pure* hexadecimal string to an integer
int hexToInt(string hexStr) {
    if (hexStr.empty()) return 0;
    
    unsigned int value;
    stringstream ss;
    ss << hex << hexStr;
    ss >> value;
    return value;
}

// Converts an ASCII string to its hex representation
string stringToHex(const string& str) {
    stringstream ss;
    for (char c : str) {
        ss << hex << uppercase << setfill('0') << setw(2) << (int)c;
    }
    return ss.str();
}

// Checks if a string is a decimal number
bool isNumber(const string& s) {
    return !s.empty() && all_of(s.begin(), s.end(), ::isdigit);
}

// Gets the *absolute* address of an operand (for BASE)
string getOperandValue(const string& operand) {
    if (operand.empty()) return "0";
    if (isNumber(operand)) return operand; // Numeric literal

    if (SYMTAB.count(operand)) {
        SymbolInfo sym = SYMTAB[operand];
        int targetAddress;
        if (sym.isAbsolute) {
            targetAddress = sym.address;
        } else {
            // Calculate absolute address
            targetAddress = sym.address + BLOCKTAB[BLOCK_ID_TO_NAME[sym.blockNumber]].startAddress;
        }
        return intToHex(targetAddress, 4);
    }
    
    // Fallback for hex values passed to BASE (less common)
    return intToHex(hexToInt(operand), 4);
}


// Initialize the Operation Code Table (OPTAB)
void initializeOPTAB() {
    // Format 1
    OPTAB["FIX"] = {"C4", 1};
    OPTAB["FLOAT"] = {"C0", 1};
    OPTAB["HIO"] = {"F4", 1};
    OPTAB["NORM"] = {"C8", 1};
    OPTAB["SIO"] = {"F0", 1};
    OPTAB["TIO"] = {"F8", 1};

    // Format 2
    OPTAB["ADDR"] = {"90", 2};
    OPTAB["CLEAR"] = {"B4", 2};
    OPTAB["COMPR"] = {"A0", 2};
    OPTAB["DIVR"] = {"9C", 2};
    OPTAB["MULR"] = {"98", 2};
    OPTAB["RMO"] = {"AC", 2};
    OPTAB["SHIFTL"] = {"A4", 2};
    OPTAB["SHIFTR"] = {"A8", 2};
    OPTAB["SUBR"] = {"94", 2};
    OPTAB["SVC"] = {"B0", 2};
    OPTAB["TIXR"] = {"B8", 2};

    // Format 3/4
    OPTAB["ADD"] = {"18", 3};
    OPTAB["ADDF"] = {"58", 3};
    OPTAB["AND"] = {"40", 3};
    OPTAB["COMP"] = {"28", 3};
    OPTAB["COMPF"] = {"88", 3};
    OPTAB["DIV"] = {"24", 3};
    OPTAB["DIVF"] = {"64", 3};
    OPTAB["J"] = {"3C", 3};
    OPTAB["JEQ"] = {"30", 3};
    OPTAB["JGT"] = {"34", 3};
    OPTAB["JLT"] = {"38", 3};
    OPTAB["JSUB"] = {"48", 3};
    OPTAB["LDA"] = {"00", 3};
    OPTAB["LDB"] = {"68", 3};
    OPTAB["LDCH"] = {"50", 3};
    OPTAB["LDF"] = {"70", 3};
    OPTAB["LDL"] = {"08", 3};
    OPTAB["LDS"] = {"6C", 3};
    OPTAB["LDT"] = {"74", 3};
    OPTAB["LDX"] = {"04", 3};
    OPTAB["LPS"] = {"D0", 3};
    OPTAB["MUL"] = {"20", 3};
    OPTAB["MULF"] = {"60", 3};
    OPTAB["OR"] = {"44", 3};
    OPTAB["RD"] = {"D8", 3};
    OPTAB["RSUB"] = {"4C", 3};
    OPTAB["SSK"] = {"EC", 3};
    OPTAB["STA"] = {"0C", 3};
    OPTAB["STB"] = {"78", 3};
    OPTAB["STCH"] = {"54", 3};
    OPTAB["STF"] = {"80", 3};
    OPTAB["STI"] = {"D4", 3};
    OPTAB["STL"] = {"14", 3};
    OPTAB["STS"] = {"7C", 3};
    OPTAB["STSW"] = {"E8", 3};
    OPTAB["STT"] = {"84", 3};
    OPTAB["STX"] = {"10", 3};
    OPTAB["SUB"] = {"1C", 3};
    OPTAB["SUBF"] = {"5C", 3};
    OPTAB["TD"] = {"E0", 3};
    OPTAB["TIX"] = {"2C", 3};
    OPTAB["WD"] = {"DC", 3};
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        cerr << "Usage: " << argv[0] << " <source_filename>" << endl;
        return 1;
    }

    string filename = argv[1];
    string baseFilename = filename.substr(0, filename.find_last_of('.'));

    string intermediateFile = baseFilename + ".int";
    string listingFile = baseFilename + ".lst";
    string objectFile = baseFilename + ".obj";
    
    string startAddress; // To be set by Pass 1

    try {
        initializeOPTAB();

        cout << "--- Starting Pass 1 ---" << endl;
        performPass1(filename, intermediateFile);
        cout << "--- Pass 1 Complete ---" << endl;
        
        // Get start address from the default block
        startAddress = intToHex(BLOCKTAB["DEFAULT"].startAddress, 4);

        cout << "--- Starting Pass 2 ---" << endl;
        performPass2(intermediateFile, listingFile, objectFile, startAddress);
        cout << "--- Pass 2 Complete ---" << endl;

        cout << "\nAssembly successful." << endl;
        cout << "Listing file created: " << listingFile << endl;
        cout << "Object file created: " << objectFile << endl;

    } catch (const exception& e) {
        cerr << "Assembly failed: " << e.what() << endl;
        return 1;
    }

    return 0;
}
