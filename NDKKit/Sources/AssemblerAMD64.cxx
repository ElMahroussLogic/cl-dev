/* -------------------------------------------

	Copyright ZKA Technologies

------------------------------------------- */

/////////////////////////////////////////////////////////////////////////////////////////

/// @file i64asm.cxx
/// @author Amlal EL Mahrouss
/// @brief AMD64 Assembler.

/// REMINDER: when dealing with an undefined symbol use (string
/// size):LinkerFindSymbol:(string) so that ld will look for it.

/////////////////////////////////////////////////////////////////////////////////////////

/// bugs: 0

/// feature request: 1
/// Encode registers in mov, add, xor...

/////////////////////////////////////////////////////////////////////////////////////////

#define __ASM_NEED_AMD64__ 1

#define kAssemblerPragmaSymStr "#"
#define kAssemblerPragmaSym	   '#'

#include <NDKKit/AsmKit/CPU/amd64.hxx>
#include <NDKKit/Parser.hxx>
#include <NDKKit/NFC/AE.hxx>
#include <NDKKit/NFC/PEF.hxx>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>

/////////////////////

// ANSI ESCAPE CODES

/////////////////////

#define kBlank	"\e[0;30m"
#define kRed	"\e[0;31m"
#define kWhite	"\e[0;97m"
#define kYellow "\e[0;33m"

#define kStdOut (std::cout << kWhite)
#define kStdErr (std::cout << kRed)

static char	   kOutputArch	   = NDK::kPefArchAMD64;
static Boolean kOutputAsBinary = false;

static UInt32 kErrorLimit		= 10;
static UInt32 kAcceptableErrors = 0;

constexpr auto cAMD64IPAlignment = 0x4U;

static std::size_t kCounter = 1UL;

static std::uintptr_t									   kOrigin = kPefBaseOrigin;
static std::vector<std::pair<std::string, std::uintptr_t>> kOriginLabel;

/// @brief keep it simple by default.
static std::int32_t kRegisterBitWidth = 16U;

static bool kVerbose = false;

static std::vector<i64_byte_t> kAppBytes;

static NDK::AERecordHeader kCurrentRecord{
	.fName = "", .fKind = NDK::kPefCode, .fSize = 0, .fOffset = 0};

static std::vector<NDK::AERecordHeader> kRecords;
static std::vector<std::string>			kDefinedSymbols;
static std::vector<std::string>			kUndefinedSymbols;

static const std::string kUndefinedSymbol = ":UndefinedSymbol:";
static const std::string kRelocSymbol	  = ":RuntimeSymbol:";

// \brief forward decl.
static bool asm_read_attributes(std::string& line);

#include <asmutils.hxx>

/////////////////////////////////////////////////////////////////////////////////////////

// @brief AMD64 assembler entrypoint, the program/module starts here.

/////////////////////////////////////////////////////////////////////////////////////////

NDK_MODULE(NewOSAssemblerAMD64)
{
	//////////////// CPU OPCODES BEGIN ////////////////

	std::string opcodes_jump[kJumpLimit] = {
		"ja", "jae", "jb", "jbe", "jc", "je", "jg", "jge", "jl", "jle",
		"jna", "jnae", "jnb", "jnbe", "jnc", "jne", "jng", "jnge", "jnl", "jnle",
		"jno", "jnp", "jns", "jnz", "jo", "jp", "jpe", "jpo", "js", "jz"};

	for (i64_hword_t i = 0; i < kJumpLimit; i++)
	{
		CpuOpcodeAMD64 code{
			.fName	 = opcodes_jump[i],
			.fOpcode = static_cast<i64_hword_t>(kAsmJumpOpcode + i)};
		kOpcodesAMD64.push_back(code);
	}

	CpuOpcodeAMD64 code{.fName = "jcxz", .fOpcode = 0xE3};
	kOpcodesAMD64.push_back(code);

	for (i64_hword_t i = kJumpLimitStandard; i < kJumpLimitStandardLimit; i++)
	{
		CpuOpcodeAMD64 code{.fName = "jmp", .fOpcode = i};
		kOpcodesAMD64.push_back(code);
	}

	CpuOpcodeAMD64 lahf{.fName = "lahf", .fOpcode = 0x9F};
	kOpcodesAMD64.push_back(lahf);

	CpuOpcodeAMD64 lds{.fName = "lds", .fOpcode = 0xC5};
	kOpcodesAMD64.push_back(lds);

	CpuOpcodeAMD64 lea{.fName = "lea", .fOpcode = 0x8D};
	kOpcodesAMD64.push_back(lea);

	CpuOpcodeAMD64 nop{.fName = "nop", .fOpcode = 0x90};
	kOpcodesAMD64.push_back(nop);

	//////////////// CPU OPCODES END ////////////////

	for (size_t i = 1; i < argc; ++i)
	{
		if (argv[i][0] == '/')
		{
			if (strcmp(argv[i], "/version") == 0 || strcmp(argv[i], "/v") == 0)
			{
				kStdOut << "i64asm: AMD64 Assembler Driver.\ni64asm: v1.10\ni64asm: Copyright "
						   "(c) ZKA Technologies.\n";
				return 0;
			}
			else if (strcmp(argv[i], "/h") == 0)
			{
				kStdOut << "i64asm: AMD64 Assembler Driver.\ni64asm: Copyright (c) 2024 "
						   "ZKA Technologies.\n";
				kStdOut << "/version: Print program version.\n";
				kStdOut << "/verbose: Print verbose output.\n";
				kStdOut << "/binary: Output as flat binary.\n";

				return 0;
			}
			else if (strcmp(argv[i], "/binary") == 0)
			{
				kOutputAsBinary = true;
				continue;
			}
			else if (strcmp(argv[i], "/verbose") == 0)
			{
				kVerbose = true;
				continue;
			}

			kStdOut << "i64asm: ignore " << argv[i] << "\n";
			continue;
		}

		if (!std::filesystem::exists(argv[i]))
		{
			kStdOut << "i64asm: can't open: " << argv[i] << std::endl;
			goto asm_fail_exit;
		}

		std::string object_output(argv[i]);

		for (auto& ext : kAsmFileExts)
		{
			if (object_output.find(ext) != std::string::npos)
			{
				object_output.erase(object_output.find(ext), std::strlen(ext));
			}
		}

		object_output += kOutputAsBinary ? kBinaryFileExt : kObjectFileExt;

		std::ifstream file_ptr(argv[i]);
		std::ofstream file_ptr_out(object_output, std::ofstream::binary);

		if (file_ptr_out.bad())
		{
			if (kVerbose)
			{
				kStdOut << "i64asm: error: " << strerror(errno) << "\n";
			}
		}

		std::string line;

		NDK::AEHeader hdr{0};

		memset(hdr.fPad, kAEInvalidOpcode, kAEPad);

		hdr.fMagic[0] = kAEMag0;
		hdr.fMagic[1] = kAEMag1;
		hdr.fSize	  = sizeof(NDK::AEHeader);
		hdr.fArch	  = kOutputArch;

		/////////////////////////////////////////////////////////////////////////////////////////

		// COMPILATION LOOP

		/////////////////////////////////////////////////////////////////////////////////////////

		NDK::EncoderAMD64 asm64;

		while (std::getline(file_ptr, line))
		{
			if (auto ln = asm64.CheckLine(line, argv[i]); !ln.empty())
			{
				detail::print_error_asm(ln, argv[i]);
				continue;
			}

			try
			{
				asm_read_attributes(line);
				asm64.WriteLine(line, argv[i]);
			}
			catch (const std::exception& e)
			{
				if (kVerbose)
				{
					std::string what = e.what();
					detail::print_warning_asm("exit because of: " + what, "i64asm");
				}

				try
				{
					std::filesystem::remove(object_output);
				}
				catch (...)
				{
				}

				goto asm_fail_exit;
			}
		}

		if (!kOutputAsBinary)
		{
			if (kVerbose)
			{
				kStdOut << "i64asm: Writing object file...\n";
			}

			// this is the final step, write everything to the file.

			auto pos = file_ptr_out.tellp();

			hdr.fCount = kRecords.size() + kUndefinedSymbols.size();

			file_ptr_out << hdr;

			if (kRecords.empty())
			{
				kStdErr << "i64asm: At least one record is needed to write an object "
						   "file.\ni64asm: Make one using `export .code64 foo_bar`.\n";

				std::filesystem::remove(object_output);
				return -1;
			}

			kRecords[kRecords.size() - 1].fSize = kAppBytes.size();

			std::size_t record_count = 0UL;

			for (auto& rec : kRecords)
			{
				if (kVerbose)
					kStdOut << "i64asm: Wrote record " << rec.fName << " to file...\n";

				rec.fFlags |= NDK::kKindRelocationAtRuntime;
				rec.fOffset = record_count;
				++record_count;

				file_ptr_out << rec;
			}

			// increment once again, so that we won't lie about the kUndefinedSymbols.
			++record_count;

			for (auto& sym : kUndefinedSymbols)
			{
				NDK::AERecordHeader _record_hdr{0};

				if (kVerbose)
					kStdOut << "i64asm: Wrote symbol " << sym << " to file...\n";

				_record_hdr.fKind	= kAEInvalidOpcode;
				_record_hdr.fSize	= sym.size();
				_record_hdr.fOffset = record_count;

				++record_count;

				memset(_record_hdr.fPad, kAEInvalidOpcode, kAEPad);
				memcpy(_record_hdr.fName, sym.c_str(), sym.size());

				file_ptr_out << _record_hdr;

				++kCounter;
			}

			auto pos_end = file_ptr_out.tellp();

			file_ptr_out.seekp(pos);

			hdr.fStartCode = pos_end;
			hdr.fCodeSize  = kAppBytes.size();

			file_ptr_out << hdr;

			file_ptr_out.seekp(pos_end);
		}
		else
		{
			if (kVerbose)
			{
				kStdOut << "i64asm: Write raw binary...\n";
			}
		}

		// byte from byte, we write this.
		for (auto& byte : kAppBytes)
		{
			if (byte == 0)
				continue;

			if (byte == 0xFF)
			{
				byte = 0;
			}

			file_ptr_out << reinterpret_cast<const char*>(&byte)[0];
		}

		if (kVerbose)
			kStdOut << "i64asm: Wrote file with program in it.\n";

		file_ptr_out.flush();
		file_ptr_out.close();

		if (kVerbose)
			kStdOut << "i64asm: Exit succeeded.\n";

		return 0;
	}

asm_fail_exit:

	if (kVerbose)
		kStdOut << "i64asm: Exit failed.\n";

	return -1;
}

/////////////////////////////////////////////////////////////////////////////////////////

// @brief Check for attributes
// returns true if any was found.

/////////////////////////////////////////////////////////////////////////////////////////

static bool asm_read_attributes(std::string& line)
{
	// import is the opposite of export, it signals to the ld
	// that we need this symbol.
	if (NDK::find_word(line, "import"))
	{
		if (kOutputAsBinary)
		{
			detail::print_error_asm("Invalid directive in flat binary mode.", "i64asm");
			throw std::runtime_error("invalid_import_bin");
		}

		auto name = line.substr(line.find("import") + strlen("import") + 1);

		if (name.size() == 0)
		{
			detail::print_error_asm("Invalid import", "power-as");
			throw std::runtime_error("invalid_import");
		}

		std::string result = std::to_string(name.size());
		result += kUndefinedSymbol;

		// mangle this
		for (char& j : name)
		{
			if (j == ' ' || j == ',')
				j = '$';
		}

		result += name;

		if (name.find(".code64") != std::string::npos)
		{
			// data is treated as code.
			kCurrentRecord.fKind = NDK::kPefCode;
		}
		else if (name.find(".data64") != std::string::npos)
		{
			// no code will be executed from here.
			kCurrentRecord.fKind = NDK::kPefData;
		}
		else if (name.find(".zero64") != std::string::npos)
		{
			// this is a bss section.
			kCurrentRecord.fKind = NDK::kPefZero;
		}

		// this is a special case for the start stub.
		// we want this so that ld can find it.

		if (name == kPefStart)
		{
			kCurrentRecord.fKind = NDK::kPefCode;
		}

		// now we can tell the code size of the previous kCurrentRecord.

		if (!kRecords.empty())
			kRecords[kRecords.size() - 1].fSize = kAppBytes.size();

		memset(kCurrentRecord.fName, 0, kAESymbolLen);
		memcpy(kCurrentRecord.fName, result.c_str(), result.size());

		++kCounter;

		memset(kCurrentRecord.fPad, kAEInvalidOpcode, kAEPad);

		kRecords.emplace_back(kCurrentRecord);

		return true;
	}
	// export is a special keyword used by i64asm to tell the AE output stage to
	// mark this section as a header. it currently supports .code64, .data64 and
	// .zero64.
	else if (NDK::find_word(line, "export"))
	{
		if (kOutputAsBinary)
		{
			detail::print_error_asm("Invalid directive in flat binary mode.", "i64asm");
			throw std::runtime_error("invalid_export_bin");
		}

		auto name = line.substr(line.find("export") + strlen("export") + 1);

		std::string name_copy = name;

		for (char& j : name)
		{
			if (j == ' ')
				j = '$';
		}

		if (std::find(kDefinedSymbols.begin(), kDefinedSymbols.end(), name) !=
			kDefinedSymbols.end())
		{
			detail::print_error_asm("Symbol already defined.", "i64asm");
			throw std::runtime_error("invalid_export_bin");
		}

		kDefinedSymbols.push_back(name);

		if (name.find(".code64") != std::string::npos)
		{
			// data is treated as code.

			name_copy.erase(name_copy.find(".code64"), strlen(".code64"));
			kCurrentRecord.fKind = NDK::kPefCode;
		}
		else if (name.find(".data64") != std::string::npos)
		{
			// no code will be executed from here.

			name_copy.erase(name_copy.find(".data64"), strlen(".data64"));
			kCurrentRecord.fKind = NDK::kPefData;
		}
		else if (name.find(".zero64") != std::string::npos)
		{
			// this is a bss section.

			name_copy.erase(name_copy.find(".zero64"), strlen(".zero64"));
			kCurrentRecord.fKind = NDK::kPefZero;
		}

		// this is a special case for the start stub.
		// we want this so that ld can find it.

		if (name == kPefStart)
		{
			kCurrentRecord.fKind = NDK::kPefCode;
		}

		while (name_copy.find(" ") != std::string::npos)
			name_copy.erase(name_copy.find(" "), 1);

		kOriginLabel.push_back(std::make_pair(name_copy, kOrigin));
		++kOrigin;

		// now we can tell the code size of the previous kCurrentRecord.

		if (!kRecords.empty())
			kRecords[kRecords.size() - 1].fSize = kAppBytes.size();

		memset(kCurrentRecord.fName, 0, kAESymbolLen);
		memcpy(kCurrentRecord.fName, name.c_str(), name.size());

		++kCounter;

		memset(kCurrentRecord.fPad, kAEInvalidOpcode, kAEPad);

		kRecords.emplace_back(kCurrentRecord);

		return true;
	}

	return false;
}

// \brief algorithms and helpers.

namespace detail::algorithm
{
	// \brief authorize a brief set of characters.
	static inline bool is_not_alnum_space(char c)
	{
		return !(isalpha(c) || isdigit(c) || (c == ' ') || (c == '\t') ||
				 (c == ',') || (c == '(') || (c == ')') || (c == '"') ||
				 (c == '\'') || (c == '[') || (c == ']') || (c == '+') ||
				 (c == '_') || (c == ':') || (c == '@') || (c == '.') || (c == '#'));
	}

	bool is_valid_amd64(const std::string& str)
	{
		return std::find_if(str.begin(), str.end(), is_not_alnum_space) == str.end();
	}
} // namespace detail::algorithm

/////////////////////////////////////////////////////////////////////////////////////////

// @brief Check for line (syntax check)

/////////////////////////////////////////////////////////////////////////////////////////

std::string NDK::EncoderAMD64::CheckLine(std::string&		line,
										 const std::string& file)
{
	std::string err_str;

	if (line.empty() || NDK::find_word(line, "import") ||
		NDK::find_word(line, "export") ||
		NDK::find_word(line, kAssemblerPragmaSymStr) ||
		NDK::find_word(line, ";") || line[0] == kAssemblerPragmaSym)
	{
		if (line.find(';') != std::string::npos)
		{
			line.erase(line.find(';'));
		}
		else
		{
			// now check the line for validity
			if (!detail::algorithm::is_valid_amd64(line))
			{
				err_str = "Line contains non valid characters.\nhere -> ";
				err_str += line;
			}
		}

		return err_str;
	}

	if (!detail::algorithm::is_valid_amd64(line))
	{
		err_str = "Line contains non alphanumeric characters.\nHere -> ";
		err_str += line;

		return err_str;
	}

	// check for a valid instruction format.

	if (line.find(',') != std::string::npos)
	{
		if (line.find(',') + 1 == line.size())
		{
			err_str += "\nInstruction lacks right register, here -> ";
			err_str += line.substr(line.find(','));

			return err_str;
		}
		else
		{
			bool nothing_on_right = true;

			if (line.find(',') + 1 > line.size())
			{
				err_str += "\nInstruction not complete, here -> ";
				err_str += line;

				return err_str;
			}

			auto substr = line.substr(line.find(',') + 1);

			for (auto& ch : substr)
			{
				if (ch != ' ' && ch != '\t')
				{
					nothing_on_right = false;
				}
			}

			// this means we found nothing after that ',' .
			if (nothing_on_right)
			{
				err_str += "\nInstruction not complete, here -> ";
				err_str += line;

				return err_str;
			}
		}
	}
	for (auto& opcodeAMD64 : kOpcodesAMD64)
	{
		if (NDK::find_word(line, opcodeAMD64.fName))
		{
			return err_str;
		}
	}

	err_str += "\nUnrecognized instruction -> " + line;

	return err_str;
}

bool NDK::EncoderAMD64::WriteNumber(const std::size_t& pos,
									std::string&	   jump_label)
{
	if (!isdigit(jump_label[pos]))
		return false;

	switch (jump_label[pos + 1])
	{
	case 'x': {
		if (auto res = strtol(jump_label.substr(pos + 2).c_str(), nullptr, 16);
			!res)
		{
			if (errno != 0)
			{
				detail::print_error_asm("invalid hex number: " + jump_label, "i64asm");
				throw std::runtime_error("invalid_hex");
			}
		}

		NDK::NumberCast64 num = NDK::NumberCast64(
			strtol(jump_label.substr(pos + 2).c_str(), nullptr, 16));

		for (char& i : num.number)
		{
			if (i == 0)
				i = 0xFF;

			kAppBytes.push_back(i);
		}

		if (kVerbose)
		{
			kStdOut << "i64asm: Found a base 16 number here: "
					<< jump_label.substr(pos) << "\n";
		}

		return true;
	}
	case 'b': {
		if (auto res = strtol(jump_label.substr(pos + 2).c_str(), nullptr, 2);
			!res)
		{
			if (errno != 0)
			{
				detail::print_error_asm("invalid binary number: " + jump_label, "i64asm");
				throw std::runtime_error("invalid_bin");
			}
		}

		NDK::NumberCast64 num = NDK::NumberCast64(
			strtol(jump_label.substr(pos + 2).c_str(), nullptr, 2));

		if (kVerbose)
		{
			kStdOut << "i64asm: Found a base 2 number here: "
					<< jump_label.substr(pos) << "\n";
		}

		for (char& i : num.number)
		{
			if (i == 0)
				i = 0xFF;

			kAppBytes.push_back(i);
		}

		return true;
	}
	case 'o': {
		if (auto res = strtol(jump_label.substr(pos + 2).c_str(), nullptr, 7);
			!res)
		{
			if (errno != 0)
			{
				detail::print_error_asm("invalid octal number: " + jump_label, "i64asm");
				throw std::runtime_error("invalid_octal");
			}
		}

		NDK::NumberCast64 num = NDK::NumberCast64(
			strtol(jump_label.substr(pos + 2).c_str(), nullptr, 7));

		if (kVerbose)
		{
			kStdOut << "i64asm: Found a base 8 number here: "
					<< jump_label.substr(pos) << "\n";
		}

		for (char& i : num.number)
		{
			if (i == 0)
				i = 0xFF;

			kAppBytes.push_back(i);
		}

		return true;
	}
	default: {
		break;
	}
	}

	/* check for errno and stuff like that */
	if (auto res = strtol(jump_label.substr(pos).c_str(), nullptr, 10); !res)
	{
		if (errno != 0)
		{
			return false;
		}
	}

	NDK::NumberCast64 num = NDK::NumberCast64(
		strtol(jump_label.substr(pos).c_str(), nullptr, 10));

	for (char& i : num.number)
	{
		if (i == 0)
			i = 0xFF;

		kAppBytes.push_back(i);
	}

	if (kVerbose)
	{
		kStdOut << "i64asm: Found a base 10 number here: " << jump_label.substr(pos)
				<< "\n";
	}

	return true;
}

bool NDK::EncoderAMD64::WriteNumber32(const std::size_t& pos,
									  std::string&		 jump_label)
{
	if (!isdigit(jump_label[pos]))
		return false;

	switch (jump_label[pos + 1])
	{
	case 'x': {
		auto res = strtol(jump_label.substr(pos + 2).c_str(), nullptr, 16);
		res += kOrigin;

		if (errno != 0)
		{
			return false;
		}

		NDK::NumberCast32 num = NDK::NumberCast32(res);

		for (char& i : num.number)
		{
			if (i == 0)
				i = 0xFF;

			kAppBytes.push_back(i);
		}

		if (kVerbose)
		{
			kStdOut << "i64asm: Found a base 16 number here: "
					<< jump_label.substr(pos) << "\n";
		}

		return true;
	}
	case 'b': {
		auto res = strtol(jump_label.substr(pos + 2).c_str(), nullptr, 2);
		res += kOrigin;

		if (errno != 0)
		{
			return false;
		}

		NDK::NumberCast32 num = NDK::NumberCast32(res);

		if (kVerbose)
		{
			kStdOut << "i64asm: Found a base 2 number here: "
					<< jump_label.substr(pos) << "\n";
		}

		for (char& i : num.number)
		{
			if (i == 0)
				i = 0xFF;

			kAppBytes.push_back(i);
		}

		return true;
	}
	case 'o': {
		auto res = strtol(jump_label.substr(pos + 2).c_str(), nullptr, 7);
		res += kOrigin;

		if (errno != 0)
		{
			return false;
		}

		NDK::NumberCast32 num = NDK::NumberCast32(res);

		if (kVerbose)
		{
			kStdOut << "i64asm: Found a base 8 number here: "
					<< jump_label.substr(pos) << "\n";
		}

		for (char& i : num.number)
		{
			if (i == 0)
				i = 0xFF;

			kAppBytes.push_back(i);
		}

		return true;
	}
	default: {
		break;
	}
	}

	auto res = strtol(jump_label.substr(pos + 2).c_str(), nullptr, 10);
	res += kOrigin;

	if (errno != 0)
	{
		return false;
	}

	NDK::NumberCast32 num = NDK::NumberCast32(res);

	for (char& i : num.number)
	{
		if (i == 0)
			i = 0xFF;

		kAppBytes.push_back(i);
	}

	if (kVerbose)
	{
		kStdOut << "i64asm: Found a base 10 number here: " << jump_label.substr(pos)
				<< "\n";
	}

	return true;
}

bool NDK::EncoderAMD64::WriteNumber16(const std::size_t& pos,
									  std::string&		 jump_label)
{
	if (!isdigit(jump_label[pos]))
		return false;

	switch (jump_label[pos + 1])
	{
	case 'x': {
		if (auto res = strtol(jump_label.substr(pos + 2).c_str(), nullptr, 16);
			!res)
		{
			if (errno != 0)
			{
				detail::print_error_asm("invalid hex number: " + jump_label, "i64asm");
				throw std::runtime_error("invalid_hex");
			}
		}

		NDK::NumberCast16 num = NDK::NumberCast16(
			strtol(jump_label.substr(pos + 2).c_str(), nullptr, 16));

		for (char& i : num.number)
		{
			if (i == 0)
				i = 0xFF;

			kAppBytes.push_back(i);
		}

		if (kVerbose)
		{
			kStdOut << "i64asm: Found a base 16 number here: "
					<< jump_label.substr(pos) << "\n";
		}

		return true;
	}
	case 'b': {
		if (auto res = strtol(jump_label.substr(pos + 2).c_str(), nullptr, 2);
			!res)
		{
			if (errno != 0)
			{
				detail::print_error_asm("invalid binary number: " + jump_label, "i64asm");
				throw std::runtime_error("invalid_bin");
			}
		}

		NDK::NumberCast16 num = NDK::NumberCast16(
			strtol(jump_label.substr(pos + 2).c_str(), nullptr, 2));

		if (kVerbose)
		{
			kStdOut << "i64asm: Found a base 2 number here: "
					<< jump_label.substr(pos) << "\n";
		}

		for (char& i : num.number)
		{
			if (i == 0)
				i = 0xFF;

			kAppBytes.push_back(i);
		}

		return true;
	}
	case 'o': {
		if (auto res = strtol(jump_label.substr(pos + 2).c_str(), nullptr, 7);
			!res)
		{
			if (errno != 0)
			{
				detail::print_error_asm("invalid octal number: " + jump_label, "i64asm");
				throw std::runtime_error("invalid_octal");
			}
		}

		NDK::NumberCast16 num = NDK::NumberCast16(
			strtol(jump_label.substr(pos + 2).c_str(), nullptr, 7));

		if (kVerbose)
		{
			kStdOut << "i64asm: Found a base 8 number here: "
					<< jump_label.substr(pos) << "\n";
		}

		for (char& i : num.number)
		{
			if (i == 0)
				i = 0xFF;

			kAppBytes.push_back(i);
		}

		return true;
	}
	default: {
		break;
	}
	}

	/* check for errno and stuff like that */
	if (auto res = strtol(jump_label.substr(pos).c_str(), nullptr, 10); !res)
	{
		if (errno != 0)
		{
			return false;
		}
	}

	NDK::NumberCast16 num = NDK::NumberCast16(
		strtol(jump_label.substr(pos).c_str(), nullptr, 10));

	for (char& i : num.number)
	{
		if (i == 0)
			i = 0xFF;

		kAppBytes.push_back(i);
	}

	if (kVerbose)
	{
		kStdOut << "i64asm: Found a base 10 number here: " << jump_label.substr(pos)
				<< "\n";
	}

	return true;
}

bool NDK::EncoderAMD64::WriteNumber8(const std::size_t& pos,
									 std::string&		jump_label)
{
	if (!isdigit(jump_label[pos]))
		return false;

	switch (jump_label[pos + 1])
	{
	case 'x': {
		if (auto res = strtol(jump_label.substr(pos + 2).c_str(), nullptr, 16);
			!res)
		{
			if (errno != 0)
			{
				detail::print_error_asm("invalid hex number: " + jump_label, "i64asm");
				throw std::runtime_error("invalid_hex");
			}
		}

		NDK::NumberCast8 num = NDK::NumberCast8(
			strtol(jump_label.substr(pos + 2).c_str(), nullptr, 16));

		kAppBytes.push_back(num.number);

		if (kVerbose)
		{
			kStdOut << "i64asm: Found a base 16 number here: "
					<< jump_label.substr(pos) << "\n";
		}

		return true;
	}
	case 'b': {
		if (auto res = strtol(jump_label.substr(pos + 2).c_str(), nullptr, 2);
			!res)
		{
			if (errno != 0)
			{
				detail::print_error_asm("invalid binary number: " + jump_label, "i64asm");
				throw std::runtime_error("invalid_bin");
			}
		}

		NDK::NumberCast8 num = NDK::NumberCast8(
			strtol(jump_label.substr(pos + 2).c_str(), nullptr, 2));

		if (kVerbose)
		{
			kStdOut << "i64asm: Found a base 2 number here: "
					<< jump_label.substr(pos) << "\n";
		}

		kAppBytes.push_back(num.number);

		return true;
	}
	case 'o': {
		if (auto res = strtol(jump_label.substr(pos + 2).c_str(), nullptr, 7);
			!res)
		{
			if (errno != 0)
			{
				detail::print_error_asm("invalid octal number: " + jump_label, "i64asm");
				throw std::runtime_error("invalid_octal");
			}
		}

		NDK::NumberCast8 num = NDK::NumberCast8(
			strtol(jump_label.substr(pos + 2).c_str(), nullptr, 7));

		if (kVerbose)
		{
			kStdOut << "i64asm: Found a base 8 number here: "
					<< jump_label.substr(pos) << "\n";
		}

		kAppBytes.push_back(num.number);

		return true;
	}
	default: {
		break;
	}
	}

	/* check for errno and stuff like that */
	if (auto res = strtol(jump_label.substr(pos).c_str(), nullptr, 10); !res)
	{
		if (errno != 0)
		{
			return false;
		}
	}

	NDK::NumberCast8 num = NDK::NumberCast8(
		strtol(jump_label.substr(pos).c_str(), nullptr, 10));

	kAppBytes.push_back(num.number);

	if (kVerbose)
	{
		kStdOut << "i64asm: Found a base 10 number here: " << jump_label.substr(pos)
				<< "\n";
	}

	return true;
}

/////////////////////////////////////////////////////////////////////////////////////////

// @brief Read and write an instruction to the output array.

/////////////////////////////////////////////////////////////////////////////////////////

bool NDK::EncoderAMD64::WriteLine(std::string&		 line,
								  const std::string& file)
{
	if (NDK::find_word(line, "export "))
		return true;

	struct RegMapAMD64
	{
		std::string fName;
		i64_byte_t	fModRM;
	};

	std::vector<RegMapAMD64> REGISTER_LIST{
		{.fName = "ax", .fModRM = 0x0},
		{.fName = "cx", .fModRM = 1},
		{.fName = "dx", .fModRM = 0x2},
		{.fName = "bx", .fModRM = 3},
		{.fName = "sp", .fModRM = 0x4},
		{.fName = "bp", .fModRM = 5},
		{.fName = "si", .fModRM = 0x6},
		{.fName = "di", .fModRM = 7},
		{.fName = "r8", .fModRM = 8},
		{.fName = "r13", .fModRM = 9},
		{.fName = "r9", .fModRM = 10},
		{.fName = "r14", .fModRM = 11},
		{.fName = "r10", .fModRM = 12},
		{.fName = "r15", .fModRM = 13},
		{.fName = "r11", .fModRM = 14},
	};

	bool foundInstruction = false;

	for (auto& opcodeAMD64 : kOpcodesAMD64)
	{
		// strict check here
		if (NDK::find_word(line, opcodeAMD64.fName) &&
			detail::algorithm::is_valid_amd64(line))
		{
			foundInstruction = true;
			std::string name(opcodeAMD64.fName);

			/// Move instruction handler.
			if (name.find("mov") != std::string::npos)
			{
				std::string substr = line.substr(line.find(name) + name.size());

				uint64_t bits = kRegisterBitWidth;

				if (substr.find(",") == std::string::npos)
				{
					detail::print_error_asm("Syntax error.", "i64asm");
					throw std::runtime_error("syntax_err");
				}

				bool onlyOneReg = true;

				std::vector<RegMapAMD64> currentRegList;

				for (auto& reg : REGISTER_LIST)
				{
					std::vector<char> regExt = {'e', 'r'};

					for (auto& ext : regExt)
					{
						std::string registerName;

						if (bits > 16)
							registerName.push_back(ext);

						registerName += reg.fName;

						while (line.find(registerName) != std::string::npos)
						{
							line.erase(line.find(registerName), registerName.size());

							if (bits == 16)
							{
								if (registerName[0] == 'r')
								{
									detail::print_error_asm(
										"invalid size for register, current bit width is: " +
											std::to_string(kRegisterBitWidth),
										file);
									throw std::runtime_error("invalid_reg_size");
								}
							}

							currentRegList.push_back(
								{.fName = registerName, .fModRM = reg.fModRM});
						}
					}
				}

				if (currentRegList.size() > 1)
					onlyOneReg = false;

				bool hasRBasedRegs = false;

				if (!onlyOneReg)
				{
					/// very tricky to understand.
					/// but this checks for a r8 through r15 register.
					if (currentRegList[0].fName[0] == 'r' ||
						currentRegList[1].fName[0] == 'r')
					{
						if (isdigit(currentRegList[0].fName[1]) &&
							isdigit(currentRegList[1].fName[1]))
						{
							kAppBytes.emplace_back(0x4d);
							hasRBasedRegs = true;
						}
						else if (isdigit(currentRegList[0].fName[1]) ||
								 isdigit(currentRegList[1].fName[1]))
						{
							kAppBytes.emplace_back(0x4c);
							hasRBasedRegs = true;
						}
					}
				}

				if (bits == 64 || bits == 32)
				{
					if (!hasRBasedRegs && bits >= 32)
					{
						kAppBytes.emplace_back(opcodeAMD64.fOpcode);
					}

					kAppBytes.emplace_back(0x89);
				}
				else if (bits == 16)
				{
					if (hasRBasedRegs)
					{
						detail::print_error_asm(
							"Invalid combination of operands and registers.", "i64asm");
						throw std::runtime_error("comb_op_reg");
					}

					kAppBytes.emplace_back(0x66);
					kAppBytes.emplace_back(0x89);
				}

				if (currentRegList[1].fName[0] == 'r' &&
					currentRegList[0].fName[0] == 'e')
				{
					detail::print_error_asm("Invalid combination of operands and registers.",
										"i64asm");
					throw std::runtime_error("comb_op_reg");
				}

				if (currentRegList[0].fName[0] == 'r' &&
					currentRegList[1].fName[0] == 'e')
				{
					detail::print_error_asm("Invalid combination of operands and registers.",
										"i64asm");
					throw std::runtime_error("comb_op_reg");
				}

				if (bits == 16)
				{
					if (currentRegList[0].fName[0] == 'r' ||
						currentRegList[0].fName[0] == 'e')
					{
						detail::print_error_asm("Invalid combination of operands and registers.",
											"i64asm");
						throw std::runtime_error("comb_op_reg");
					}

					if (currentRegList[1].fName[0] == 'r' ||
						currentRegList[1].fName[0] == 'e')
					{
						detail::print_error_asm("Invalid combination of operands and registers.",
											"i64asm");
						throw std::runtime_error("comb_op_reg");
					}
				}
				else
				{
					if (currentRegList[0].fName[0] != 'r' ||
						currentRegList[0].fName[0] == 'e')
					{
						detail::print_error_asm("Invalid combination of operands and registers.",
											"i64asm");
						throw std::runtime_error("comb_op_reg");
					}

					if (currentRegList[1].fName[0] != 'r' ||
						currentRegList[1].fName[0] == 'e')
					{
						detail::print_error_asm("Invalid combination of operands and registers.",
											"i64asm");
						throw std::runtime_error("comb_op_reg");
					}
				}

				/// encode register using the modrm encoding.

				auto modrm = (0x3 << 6 | currentRegList[1].fModRM << 3 |
							  currentRegList[0].fModRM);

				kAppBytes.emplace_back(modrm);

				break;
			}
			else if (name == "int" || name == "into" || name == "intd")
			{
				kAppBytes.emplace_back(opcodeAMD64.fOpcode);
				this->WriteNumber8(line.find(name) + name.size() + 1, line);

				break;
			}
			else if (name == "jmp" || name == "call")
			{
				kAppBytes.emplace_back(opcodeAMD64.fOpcode);

				if (!this->WriteNumber32(line.find(name) + name.size() + 1, line))
				{
					throw std::runtime_error("BUG: WriteNumber32");
				}

				break;
			}
			else
			{
				kAppBytes.emplace_back(opcodeAMD64.fOpcode);

				break;
			}
		}
	}

	if (line[0] == kAssemblerPragmaSym)
	{
		if (foundInstruction)
		{
			detail::print_error_asm("Syntax error: " + line, "i64asm");
			throw std::runtime_error("syntax_err");
		}

		if (line.find("bits 64") != std::string::npos)
		{
			kRegisterBitWidth = 64U;
		}
		else if (line.find("bits 32") != std::string::npos)
		{
			kRegisterBitWidth = 32U;
		}
		else if (line.find("bits 16") != std::string::npos)
		{
			kRegisterBitWidth = 16U;
		}
		else if (line.find("org") != std::string::npos)
		{
			size_t base[] = {10, 16, 2, 7};

			for (size_t i = 0; i < 4; i++)
			{
				if (kOrigin = strtol(
						(line.substr(line.find("org") + strlen("org") + 1)).c_str(),
						nullptr, base[i]);
					kOrigin)
				{
					if (errno != 0)
					{
						continue;
					}
					else
					{
						if (kVerbose)
						{
							kStdOut << "i64asm: origin set: " << kOrigin << std::endl;
						}

						break;
					}
				}
			}
		}
	}
	/// write a dword
	else if (line.find(".dword") != std::string::npos)
	{
		this->WriteNumber32(line.find(".dword") + strlen(".dword") + 1, line);
	}
	/// write a long
	else if (line.find(".long") != std::string::npos)
	{
		this->WriteNumber(line.find(".long") + strlen(".long") + 1, line);
	}
	/// write a 16-bit number
	else if (line.find(".word") != std::string::npos)
	{
		this->WriteNumber16(line.find(".word") + strlen(".word") + 1, line);
	}

	kOrigin += cAMD64IPAlignment;

	return true;
}

// Last rev 13-1-24