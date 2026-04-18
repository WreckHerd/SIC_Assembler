#ifndef SIC_ASSEMBLER_H
#define SIC_ASSEMBLER_H

#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <map>
#include <vector>
#include <iomanip>
#include <algorithm>
#include <stdexcept> // For runtime_error

using namespace std;

// --- Data Structures ---

// Operation Code Table (OPTAB)
struct OpInfo {
    string opcode;
    int format;
};

// Symbol Table (SYMTAB)
struct SymbolInfo {
    int address;
    int blockNumber;
    bool isAbsolute; // True if defined by EQU
};

// Program Block Table (BLOCKTAB)
struct BlockInfo {
    int blockNumber;
    int locctr;
    int startAddress;
};

// Literal Table (LITTAB)
struct LiteralInfo {
    int address;
    int blockNumber;
    int size;
    string value; // The hex value of the literal
};

// --- Global Tables ---
extern map<string, OpInfo> OPTAB;
extern map<string, SymbolInfo> SYMTAB;
extern map<string, BlockInfo> BLOCKTAB;
extern map<string, LiteralInfo> LITTAB;
extern map<int, string> BLOCK_ID_TO_NAME; // Reverse map for easy lookup

// --- Global Variables ---
extern int programLength;
extern int currentBlockNumber;
extern string baseRegisterValue;
extern bool baseRegisterAvailable;

// --- Utility Functions ---
string intToHex(int n, int width = 6);
int hexToInt(string hexStr);
string stringToHex(const string& str);
string getOperandValue(const string& operand); // Simplified
bool isNumber(const string& s);
void initializeOPTAB();

#endif // SIC_ASSEMBLER_H
