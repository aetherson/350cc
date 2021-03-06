#include "Assembler.h"
#include <iostream>
#include <vector>
#include <sstream>

using namespace std;

map<string, int> Assembler::opcodes = { { "add", 0x0 }, { "slt", 0x2 }, { "lea", 0xc }, { "call", 0xd }, { "brz", 0xf }, { "ld", 0xa }, { "st", 0xb } };
map<string, int> Assembler::registers = { { "r0", 0x0 }, { "r1", 0x1 }, { "r2", 0x2 }, { "r3", 0x3 }, { "r4", 0x4 }, { "r5", 0x5 }, { "r6", 0x6 }, { "r7", 0x7 } };
map<string, SpecialOpcodes> Assembler::specialInstructions = { { "callFunc", callFunc }, { "ret", ret }, { "push", push }, { "pop", pop } };

string Assembler::formatOutput(string s)
{
	while (s.length() < 4)
	{
		s = "0" + s;
	}

	return s;
}

bool Assembler::isCommented(const string& s)
{
	const int hasComment = s.find("//");
	return hasComment >= 0;
}

string Assembler::createAssembledLine(const Instruction& instruction)
{
	int output = (instruction.opcode << 12) + (instruction.format << 11) + (instruction.rd << 8);
	switch (instruction.type)
	{
	case A:
		output += instruction.ra << 5;
		output += instruction.rb;
		break;

	case B:
		output += instruction.ra << 5;
		output += instruction.imm5 & 0x1f;
		break;

	case C:
		output += instruction.imm8 & 0xff;
		break;
	}

	char outputs[256];
#ifdef _WIN32
	sprintf_s(outputs, "%x", output);
#else
	sprintf(outputs, "%x", output);
#endif
	return string(outputs);
}

Instruction Assembler::parseLine(const string& line, int currentLine)
{
	Instruction parsedInstruction;
	parsedInstruction.invalid = false;
	parsedInstruction.skipAmount = 0;

	//get opcode
	stringstream lineStream(line);

	string instructionName;
	lineStream >> instructionName;

	if (Assembler::isCommented(instructionName))
	{
		parsedInstruction.invalid = true;
		return parsedInstruction;
	}

	const int hasPeriod = instructionName.find(".");
	if (hasPeriod >= 0)
	{
		parsedInstruction.invalid = true;

		//check if data
		if (instructionName == ".data")
		{
			this->isParsingData = true;
		}
		else if (instructionName == ".code")
		{
			this->isParsingData = false;
		}
		else if (instructionName == ".skip")
		{
			lineStream >> parsedInstruction.skipAmount;
		}

		return parsedInstruction;
	}

	if (this->isParsingData)
	{
		const int isHex = line.find("0x");
		if (isHex < 0)
		{
			stringstream nStream(line);
			nStream >> parsedInstruction.data;
		}
		else
		{
			stringstream nStream(line.substr(2));
			int res = 0;
			nStream >> std::hex >> res;
			parsedInstruction.data = static_cast<short>(res);		
		}

		return parsedInstruction;
	}

	const auto opcode = this->opcodes.find(instructionName);
	if (opcode == this->opcodes.end())
	{
		parsedInstruction.invalid = true;
		const int ind = instructionName.find(':');
		if (ind < 0)
		{ 
			//is not a label
			this->errorText += "ERROR: Invalid opcode " + instructionName + " at line " + to_string(currentLine) + "\n";
		}

		return parsedInstruction;
	}

	parsedInstruction.opcode = opcode->second;

	string rd;
	lineStream >> rd;
	parsedInstruction.rd = registers[rd];

	string registerOrLabelOrConstant;

	lineStream >> registerOrLabelOrConstant;

	//check if register	
	if ((parsedInstruction.ra = Assembler::readRegister(registerOrLabelOrConstant)) < 0)
	{
		//Not a register, must be an 8-bit immediate

		parsedInstruction.type = C;
		parsedInstruction.format = 1;

		parsedInstruction.imm8 = this->readImmediate(registerOrLabelOrConstant, currentLine);

		if (parsedInstruction.imm8 > 128 - 1 || parsedInstruction.imm8 < -128)
		{
			this->errorText += "Error: value " + to_string(parsedInstruction.imm8) + " for imm8 at line " + to_string(currentLine) + " does not fit in 8 bits";
			parsedInstruction.invalid = true;
		}

		return parsedInstruction;
	}

	lineStream >> registerOrLabelOrConstant;

	//check if register b

	if ((parsedInstruction.rb = Assembler::readRegister(registerOrLabelOrConstant)) >= 0)
	{
		parsedInstruction.type = A;
		parsedInstruction.format = 1;
	}
	else
	{
		parsedInstruction.type = B;
		parsedInstruction.format = 0;

		parsedInstruction.imm5 = this->readImmediate(registerOrLabelOrConstant, currentLine);

		if (parsedInstruction.imm5 > 16 - 1 || parsedInstruction.imm5 < -16)
		{
			this->errorText += "Error: value " + to_string(parsedInstruction.imm5) + " for imm5 at line " + to_string(currentLine) + " does not fit in 5 bits";
			parsedInstruction.invalid = true;
		}
	}

	return parsedInstruction;
}

//returns -1 if the token is not a register
int Assembler::readRegister(const string& token)
{
	const auto reg = Assembler::registers.find(token);
	if (reg != registers.end())
	{
		return reg->second;
	}

	return -1;
}

int Assembler::readImmediate(const string& token, int lineNumber) const
{
	int retValue = 0;

	const auto label = this->labels.find(token);
	if (label != labels.end())
	{
		retValue = label->second - lineNumber - 1;
	}
	else
	{
		//not a label, re-read the token as a number
		stringstream nStream(token);
		nStream >> retValue;
	}

	return retValue;
}

bool Assembler::findLabels(string input)
{
	stringstream inp(input);

	int labelCount = 0;
	int lineCount = 0;
	while (!inp.eof())
	{
		string nextLine;
		getline(inp, nextLine);

		if (nextLine.length() == 0)
		{
			continue;
		}

		const int ind = nextLine.find(':');
		if (ind >= 0)
		{
			const string newLabel = nextLine.substr(0, nextLine.size() - 1);
			const int hasH = newLabel.find('h');
			if (this->useHProtection && hasH > -1)
			{
				this->errorText += "ERROR: label '" + newLabel + "' contains the letter 'h'.\n";
				return false; 
			}
			this->labels[newLabel] = lineCount;
		}
		else
		{
			const int skipInd = nextLine.find(".skip");

			if (skipInd >= 0)
			{
				stringstream inpl(nextLine);
				string s;
				inpl >> s;

				int skipAmount = 0;
				inpl >> skipAmount;

				lineCount += skipAmount;
			}
			else
			{
				++lineCount;
			}
		}
	}

	return true;
}

string Assembler::GenerateStackCode()
{
	string stackCode;
	stackCode += "//Initialize stack pointer\n";
	stackCode += "lea r6 stackBegin\n";
	stackCode += "\n";
	stackCode += "//start program code here\n";
	stackCode += "\n";
	stackCode += "\n";
	stackCode += "\n";
	stackCode += "//Stack location\n";
	stackCode += ".skip 64\n";
	stackCode += "stackBegin:\n";

	return stackCode;
}

static const string scratchReg = "r5";
string Assembler::expandSpecialInstruction(const string& instruction)
{
	string expandedAssemblyCode = "";

	stringstream inp(instruction);

	string instructionName;
	inp >> instructionName;

	const SpecialOpcodes opcode = Assembler::specialInstructions[instructionName];

	string nextToken;
	inp >> nextToken;

	switch (opcode)
	{
	case push:
	{
		expandedAssemblyCode += "//push\n";
		const int reg = Assembler::readRegister(nextToken);
		if (reg >= 0)
		{
			//generate code to push register onto the stack
			expandedAssemblyCode += "st " + nextToken + " r6 0\n";
		}
		else
		{
			//generate code to push immediate onto the stack
			//uses r5 as scratch register currently
			expandedAssemblyCode += "add " + scratchReg + " r7 " + nextToken + " //" + scratchReg + " scratch reg\n";
			expandedAssemblyCode += "st " + scratchReg + " r6 0\n";
		}

		expandedAssemblyCode += "add r6 r6 -1 //decr stack ptr\n";
		break;
	}
	case pop:
	{
		expandedAssemblyCode += "//pop\n";
		const int reg = Assembler::readRegister(nextToken);

		if (reg < 0)
		{
			this->errorText += "Error: argument " + nextToken + " to 'pop' is not a valid register\n";
			return "";
		}

		expandedAssemblyCode += "ld " + nextToken + " r6 0\n";
		expandedAssemblyCode += "add r6 r6 1 //incr stack ptr\n";
		break;
	}
	case callFunc:
	{
		expandedAssemblyCode += "//callFunc\n";
		expandedAssemblyCode += "lea " + scratchReg + " 0 //" + scratchReg + " scratch\n";
		expandedAssemblyCode += "st " + scratchReg + " r6 0 //save PC on stack\n";
		expandedAssemblyCode += "add r6 r6 -1 //decr stack ptr\n";
		expandedAssemblyCode += "brz r7 " + nextToken + "\n";
		break;
	}
	case ret:
	{
		expandedAssemblyCode += "//ret\n";
		expandedAssemblyCode += "ld r5 r6 0 //load PC, r5 scratch\n";
		expandedAssemblyCode += "add r6 r6 1 //incr stack ptr\n";
		expandedAssemblyCode += "brz r7 r5 //return\n";
		break;
	}
	default:
		break;
	}

	return expandedAssemblyCode;
}

void Assembler::expandSpecialInstructions()
{
	stringstream inp(this->assemblyCode);

	while (!inp.eof())
	{
		string nextLine;
		getline(inp, nextLine);

		if (nextLine.length() == 0 || nextLine[0] == 0)
		{
			continue;
		}

		stringstream lineStream(nextLine);
		string instructionName;
		lineStream >> instructionName;

		if (Assembler::isCommented(instructionName))
		{
			continue;
		}

		const auto opcode = Assembler::specialInstructions.find(instructionName);
		if (opcode != Assembler::specialInstructions.end())
		{
			this->hasExpandedCode = true;
			const int textPos = this->assemblyCode.find(nextLine);
			this->assemblyCode.erase(textPos, nextLine.length());
			const string expandedCode = Assembler::expandSpecialInstruction(nextLine);
			this->assemblyCode.insert(textPos, expandedCode);
		}
	}
}

void Assembler::prepareInput()
{
	this->expandSpecialInstructions();
	this->findLabels(this->assemblyCode);
}

string Assembler::Assemble()
{	
	this->prepareInput();
	if (this->errorText != "")
		return "";

	vector<string> outputLines;

	stringstream inp(this->assemblyCode);

	while (!inp.eof())
	{
		string nextLine;
		getline(inp, nextLine);

		if (nextLine.length() == 0 || nextLine[0] == 0)
		{
			continue;
		}

		const Instruction nextInstruction = parseLine(nextLine, outputLines.size());

		if (!nextInstruction.invalid && !this->isParsingData)
			outputLines.push_back(createAssembledLine(nextInstruction));
		else if (nextInstruction.invalid && nextInstruction.skipAmount != 0)
		{
			for (int a = 0; a < nextInstruction.skipAmount; ++a)
			{
				outputLines.push_back("0000");
			}
		}
		if (this->isParsingData && !nextInstruction.invalid)
		{
			char outputs[256];
#ifdef _WIN32
			sprintf_s(outputs, "%x", static_cast<short>(nextInstruction.data));
#else
			sprintf(outputs, "%x", static_cast<short>(nextInstruction.data));
#endif
			string output(outputs);
			if (output.length() == 8)
			{
				output = output.substr(4);
			}

			outputLines.push_back(output);
		}
	}

	stringstream outputFile;

	outputFile << "WIDTH=16;" << endl;
	outputFile << "DEPTH=" << outputLines.size() << ";" << endl << endl;

	outputFile << "ADDRESS_RADIX=DEC;" << endl;
	outputFile << "DATA_RADIX=HEX;" << endl << endl;
	outputFile << "CONTENT BEGIN" << endl;

	int lineNumber = 0;

	for (int a = 0; a < outputLines.size(); ++a)
	{
		outputFile << lineNumber++ << ": " << formatOutput(outputLines[a]) << ";" << endl;
	}

	outputFile << "END;" << endl;

	return outputFile.str();
}