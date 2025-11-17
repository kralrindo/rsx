#pragma once

#include <game/rtech/utils/studio/studio.h>

// studiomdl QC file
namespace qc
{
	constexpr size_t s_QCMaxPath = _MAX_PATH;

	//
	// VERSION
	//

	enum MajorVersion_t : uint16_t
	{
		QC_VER_MIN = 0,

		// goldsrc
		QC_VER_04 = 4,	// hl alpha
		QC_VER_06 = 6,	// hl alpha
		QC_VER_09 = 9,	// hl beta
		QC_VER_10 = 10,	// hl, hlbs, hlop
		QC_VER_14 = 14,	// 007 nf

		// source
		QC_VER_32 = 32,	// hl2 beta
		QC_VER_35 = 35,	// hl2 beta
		QC_VER_36 = 36,	// hl2 beta
		QC_VER_37 = 37,	// hl2 beta

		QC_VER_44 = 44,	// hl2, hl2 ep2, css, dods

		QC_VER_46 = 46,	// hl2, portal
		QC_VER_47 = 47,	// unknown, used for xbox but it's completely different
		QC_VER_48 = 48,	// orange box, team fortress 2, sdk 2013
		QC_VER_49 = 49,	// p2, as, csgo, sfm, l4d, l4d2, d2

		// reSource
		QC_VER_52 = 52,	// titanfall
		QC_VER_53 = 53,	// titanfall 2
		QC_VER_54 = 54,	// apex legends

		// how to handle as it's technically 52 ??
		QC_VER_63 = 63,		// titanfall (x360)

		QC_VER_MAX = 0xFFFF,
	};

	struct Version_t
	{
		Version_t() = default;
		constexpr Version_t(const MajorVersion_t maj, const uint16_t min) : major(maj), minor(min) {}
		//Version_t(const MajorVersion_t maj, const uint16_t min) : major(maj), minor(min) {}

		MajorVersion_t major;
		uint16_t minor;  // ex. r5 pak version, csgo updating 49

		static FORCEINLINE const bool VersionSupported(const Version_t min, const Version_t max, const MajorVersion_t major, const short minor = 0)
		{
			// above or below supported range
			if (major < min.major || major > max.major)
				return false;

			// is sub version below supported?
			if (major == min.major && minor < min.minor)
				return false;

			// is sub version above supported?
			if (major == max.major && minor > max.minor)
				return false;

			return true;
		}

		void SetVersion(const MajorVersion_t maj, const uint16_t min)
		{
			major = maj;
			minor = min;
		}
	};

	constexpr Version_t s_QCVersion_MIN(QC_VER_MIN, QC_VER_MIN);
	constexpr Version_t s_QCVersion_MAX(QC_VER_MAX, QC_VER_MAX);

	constexpr Version_t s_QCVersion_OB(QC_VER_48, 0); // orange box
	constexpr Version_t s_QCVersion_TF2(QC_VER_48, 1); // boing boing
	constexpr Version_t s_QCVersion_L4D(QC_VER_48, 2); // confirm l4d is 48
	constexpr Version_t s_QCVersion_L4D2(QC_VER_48, 3); // confirm l4d is 48
	constexpr Version_t s_QCVersion_P2(QC_VER_49, 0); // should this be subversion 1? bone count changed between p2 and alien swarm, sfm also uses more bones
	constexpr Version_t s_QCVersion_CSGO(QC_VER_49, 1);
	constexpr Version_t s_QCVersion_R1(QC_VER_52, 0);
	constexpr Version_t s_QCVersion_R2(QC_VER_53, 0);
	constexpr Version_t s_QCVersion_R5_080(QC_VER_54, 8);
	constexpr Version_t s_QCVersion_R5_100(QC_VER_54, 10);
	constexpr Version_t s_QCVersion_R5_120(QC_VER_54, 12);
	constexpr Version_t s_QCVersion_R5_150(QC_VER_54, 15);
	constexpr Version_t s_QCVersion_R5_RETAIL(QC_VER_54, QC_VER_MAX);


	//
	// QCFILE
	//

	enum QCFileExt
	{
		QC_EXT_QC,
		QC_EXT_QCI,

		QC_EXT_COUNT,
	};

	constexpr const char* const s_QCFileExtensions[QCFileExt::QC_EXT_COUNT] =
	{
		".qc",
		".qci",
	};

	constexpr int maxCommandLength = 0x20000; // $texturegroup is very large (it is even larger thank you r1 titans)

	struct Command_t;
	inline const bool VerifyOrder();
	class QCFile
	{
	public:
		QCFile(const std::filesystem::path& fp, char* const buf, const size_t sz, char* cmdBuf = nullptr) : path(fp), buffer(buf, sz), cmdbuf(cmdBuf)
		{
			assertm(VerifyOrder(), "commands out of order");

			commands.reserve(128); // by default allocate for at least 128 commands, on most models this should be enough

			if (!cmdbuf)
			{
				cmdbuf = buffer.Writer();
				buffer.AdvanceWriter(maxCommandLength);
			}

			path.replace_extension(s_QCFileExtensions[QC_EXT_QC]);
		};

		const void* const WriteData(const void* data, const size_t dataSize) { return buffer.WriteBufferData(data, dataSize); };
		const char* const WriteString(const char* const str) { return buffer.WriteBufferString(str, s_QCMaxPath); }
		void* const ReserveData(const size_t dataSize)
		{
			void* const out = buffer.Writer();
			buffer.AdvanceWriter(dataSize);
			return out;
		}

		inline void AddCommand(Command_t& cmd)
		{
			commands.emplace_back(cmd);
		}

		inline Command_t* const GetCommand(const size_t i) { return &commands.at(i); }

		inline const size_t NumCommands() const { return commands.size(); }

		// TEMP (every temporary solution is four years away from a permanent one)
		bool CommandToText(const Command_t* const cmd);
		bool ParseToText(const bool useIncludeFiles);
		static void SetExportVersion(const Version_t version);

	private:
		CTextBuffer buffer;

		char* cmdbuf;
		std::vector<Command_t> commands; // CRINGE AND UNBASED

		std::filesystem::path path;
	};


	//
	// OPTION
	//

	// command option data type
	enum CommandOptionType_t : uint8_t
	{
		QC_OPT_NONE,	// no data
		QC_OPT_STRING,	// string(s)
		QC_OPT_FLOAT,	// float(s)
		QC_OPT_CHAR,	// signed 8 bit value(s)
		QC_OPT_BYTE,	// unsigned 8 bit value(s)
		QC_OPT_INT,		// signed 32 bit value(s)
		QC_OPT_UINT,	// unsigned 32 bit value(s)
		QC_OPT_PAIR,	// key and value style set of two options
		QC_OPT_ARRAY,	// array of options

		QC_OPT_TYPE_COUNT,
	};

	typedef uint16_t CommandFormat_t;
	enum CommandFormatOption_t : CommandFormat_t
	{
		QC_FMT_NONE			= 0u,		// no formating options

		QC_FMT_ARRAY		= 1 << 0,	// curly brackets around this entry
		QC_FMT_COMMENT		= 1 << 1,	// comment this entry out
		QC_FMT_WRITENAME	= 1 << 2,	// write the name of this entry
		QC_FMT_PARENTLINE	= 1 << 3,	// write this entry on the same line as its parent
		QC_FMT_NEWLINE		= 1 << 4,	// last entry on its line, not to be used with 'QC_FMT_PARENTLINE'
		QC_FMT_ARRAYSTYLE	= 1 << 5,	// set this to have the curly bracket start on the parent line
	};

	constexpr CommandFormat_t s_CommandOptionFormatDefault = QC_FMT_PARENTLINE; // write this option on the parent line
	constexpr CommandFormat_t s_CommandOptionFormatNameParentLine = (QC_FMT_WRITENAME | QC_FMT_PARENTLINE); // write this option on the parent line with its name preceeding it
	constexpr CommandFormat_t s_CommandOptionFormatNameNewLine = (QC_FMT_WRITENAME | QC_FMT_NEWLINE); // write this option on a new line below the command with its name preceeding it

	enum CommentStyle_t
	{
		QC_COMMENT_NONE,
		QC_COMMENT_ONELINE,
		QC_COMMENT_MULTILINE,
	};

	// static description of an option
	struct CommandOptionDesc_t
	{
		constexpr CommandOptionDesc_t(const CommandOptionType_t typeIn, const char* const nameIn, const CommandFormat_t fmt = s_CommandOptionFormatDefault, const Version_t min = s_QCVersion_MIN, const Version_t max = s_QCVersion_MAX) : name(nameIn),
			versionMin(min), versionMax(max), type(typeIn), format(fmt) {}

		const char* const name;

		const Version_t versionMin;
		const Version_t versionMax;
		inline const bool VersionSupported(const Version_t version) const
		{
			return Version_t::VersionSupported(versionMin, versionMax, version.major, version.minor);
		}

		// formatting options
		const CommandFormat_t format;

		const CommandOptionType_t type;
	};

	struct CommandOptionData_t
	{
		CommandOptionData_t() : value(), count(0), format(0u) {}

		union
		{
			const void* ptr;
			size_t raw;
		} value;

		// if used
		uint32_t count;

		// formatting options, inherits base format from description. done so we can add comments to options when parsing
		CommandFormat_t format;

		inline const bool ArrayBracket() const { return format & QC_FMT_ARRAY; }
		inline const bool IsComment() const { return format & QC_FMT_COMMENT; }
		inline const bool WriteName() const { return format & QC_FMT_WRITENAME; }
		inline const bool ParentLine() const { return format & QC_FMT_PARENTLINE; }
		inline const bool NewLine() const { return format & QC_FMT_NEWLINE; }

		inline void SetPtr(const void* const dataPtr, const uint32_t dataCount = 1u)
		{
			count = dataCount;
			value.ptr = dataPtr;
		}
		
		// saves a pointer to data stored within QCFile, typically used for values that are stored on stack or require modification for writing to qc
		void SavePtr(QCFile* const file, const void* const dataPtr, const uint32_t dataCount, const size_t dataSize)
		{
			const void* tmp = file->WriteData(dataPtr, dataSize);
			SetPtr(tmp, dataCount);
		}

		void SaveStr(QCFile* const file, const char* const str)
		{
			const size_t size = strnlen_s(str, s_QCMaxPath) + 1;
			SavePtr(file, str, 1, size);
		}

		inline void SetRaw(const void* const dataPtr, const uint32_t dataCount = 1u, const size_t dataSize = 0ull)
		{
			count = dataCount;

			assertm(dataSize > 0 && sizeof(value.raw) >= dataSize, "invalid size");

			memcpy_s(&value.raw, sizeof(value.raw), dataPtr, dataSize);
		}
	};

	struct CommandOption_t
	{
		CommandOption_t() : desc(nullptr), data() {}
		CommandOption_t(const CommandOptionDesc_t* const optiondesc) : desc(optiondesc), data() {}

		const CommandOptionDesc_t* desc;
		CommandOptionData_t data;

		// sets pointer value to already allocated data, good for strings and the like
		void SetPtr(const void* const ptr, const uint32_t count = 1)
		{
			data.SetPtr(ptr, count);
		}

		// saves a pointer to data stored within QCFile, typically used for values that are stored on stack or require modification for writing to qc
		void SavePtr(QCFile* const file, const void* const ptr, const uint32_t count, const size_t size)
		{
			data.SavePtr(file, ptr, count, size);
		}

		void SaveStr(QCFile* const file, const char* const str)
		{
			const size_t size = strnlen_s(str, s_QCMaxPath) + 1;
			data.SavePtr(file, str, 1, size);
		}

		void SetRaw(const void* const ptr, const uint32_t count = 1, const size_t size = 0ull)
		{
			data.SetRaw(ptr, count, size);
		}

		void SetFormat(const CommandFormat_t format)
		{
			data.format = format;
		}

		inline void Init(const CommandOptionDesc_t* const optiondesc)
		{
			desc = optiondesc;
			data.format = desc->format;
		}

		inline const uint8_t TypeAsChar() const { return static_cast<uint8_t>(desc->type); }
	};

	struct CommandOptionPair_t
	{
		CommandOptionPair_t() : data(), type() {}
		CommandOptionPair_t(const char* const string0, const char* const string1)
		{
			data[0].SetPtr(string0, 1);
			data[1].SetPtr(string1, 1);

			type[0] = CommandOptionType_t::QC_OPT_STRING;
			type[1] = CommandOptionType_t::QC_OPT_STRING;
		}

		CommandOptionPair_t(QCFile* const file, const char* const string0, const char* const string1)
		{
			data[0].SaveStr(file, string0);
			data[1].SaveStr(file, string1);

			type[0] = CommandOptionType_t::QC_OPT_STRING;
			type[1] = CommandOptionType_t::QC_OPT_STRING;
		}

		// min/max pair
		CommandOptionPair_t(const float* const float0, const float* const float1, const uint32_t count0 = 1, const uint32_t count1 = 1)
		{
			SetData(0, float0, count0, CommandOptionType_t::QC_OPT_FLOAT);
			SetData(1, float1, count1, CommandOptionType_t::QC_OPT_FLOAT);
		}

		// int key, string value pair
		CommandOptionPair_t(const int key, const char* const val)
		{
			SetData(0, &key, 1, CommandOptionType_t::QC_OPT_INT);
			SetData(1, val, 1, CommandOptionType_t::QC_OPT_STRING);
		}

		// string key, float value pair
		CommandOptionPair_t(const char* const key, const float val)
		{
			SetData(0, key, 1, CommandOptionType_t::QC_OPT_STRING);
			SetData(1, &val, 1, CommandOptionType_t::QC_OPT_FLOAT);
		}

		// string key, int value pair
		CommandOptionPair_t(const char* const key, const int val)
		{
			SetData(0, key, 1, CommandOptionType_t::QC_OPT_STRING);
			SetData(1, &val, 1, CommandOptionType_t::QC_OPT_INT);
		}

		CommandOptionData_t data[2];
		CommandOptionType_t	type[2];

		void SetData(const uint32_t entryIdx, const void* const entryData, const uint32_t entryCount, const CommandOptionType_t entryType)
		{
			if (entryIdx >= 2)
			{
				assertm(false, "invalid index");
				return;
			}

			switch (entryType)
			{
			case CommandOptionType_t::QC_OPT_STRING:
			{
				data[entryIdx].SetPtr(entryData, entryCount);

				break;
			}
			case CommandOptionType_t::QC_OPT_FLOAT:
			case CommandOptionType_t::QC_OPT_INT:
			{
				if (entryCount > 2)
					data[entryIdx].SetPtr(entryData, entryCount);
				else
					data[entryIdx].SetRaw(entryData, entryCount, 4u * entryCount);

				break;
			}
			case CommandOptionType_t::QC_OPT_BYTE:
			{
				if (entryCount > 8)
					data[entryIdx].SetPtr(entryData, entryCount);
				else
					data[entryIdx].SetRaw(entryData, entryCount, entryCount);

				break;
			}
			case CommandOptionType_t::QC_OPT_NONE:
			case CommandOptionType_t::QC_OPT_PAIR:
			default:
			{
				break;
			}
			}

			type[entryIdx] = entryType;
		}
	};

	struct CommandOptionArray_t
	{
		CommandOptionArray_t(QCFile* const file, const uint32_t optionCount, const CommandFormat_t fmt = QC_FMT_NONE) : options(nullptr), numOptions(optionCount), format(fmt), numIntendation(0u)
		{
			if (numOptions == 0u)
				return;

			const size_t dataSize = sizeof(CommandOption_t) * numOptions;
			options = reinterpret_cast<CommandOption_t* const>(file->ReserveData(dataSize));
			memset(options, 0, dataSize);
		}

		CommandOption_t* options;
		uint32_t numOptions;
		inline bool TruncateOptions(const uint32_t newOptionCount)
		{
			if (newOptionCount > numOptions)
			{
				assertm(false, "overflowed option buffer somehow");
				return false;
			}

			numOptions = newOptionCount;
			return true;
		}
		
		// formatting options
		CommandFormat_t format;
		uint16_t numIntendation;

		inline const bool ArrayBracket() const { return format & QC_FMT_ARRAY; }
		inline const bool IsComment() const { return format & QC_FMT_COMMENT; }
		inline const bool WriteName() const { return format & QC_FMT_WRITENAME; }
		inline const bool ParentLine() const { return format & QC_FMT_PARENTLINE; }
		inline const bool NewLine() const { return format & QC_FMT_NEWLINE; }
	};

	typedef void(*CommandOptionDataFunc_t)(const CommandOptionData_t* const, CTextBuffer* const);

	inline void CommandOption_WriteNone(const CommandOptionData_t* const data, CTextBuffer* const text);
	inline void CommandOption_WriteString(const CommandOptionData_t* const data, CTextBuffer* const text);
	inline void CommandOption_WriteFloat(const CommandOptionData_t* const data, CTextBuffer* const text);
	inline void CommandOption_WriteChar(const CommandOptionData_t* const data, CTextBuffer* const text);
	inline void CommandOption_WriteByte(const CommandOptionData_t* const data, CTextBuffer* const text);
	inline void CommandOption_WriteInt(const CommandOptionData_t* const data, CTextBuffer* const text);
	inline void CommandOption_WriteUInt(const CommandOptionData_t* const data, CTextBuffer* const text);
	inline void CommandOption_WritePair(const CommandOptionData_t* const data, CTextBuffer* const text);
	inline void CommandOption_WriteArray(const CommandOptionData_t* const data, CTextBuffer* const text);

	static const CommandOptionDataFunc_t s_CommandOptionDataFunc[CommandOptionType_t::QC_OPT_TYPE_COUNT] = {
		CommandOption_WriteNone,
		CommandOption_WriteString,
		CommandOption_WriteFloat,
		CommandOption_WriteChar,
		CommandOption_WriteByte,
		CommandOption_WriteInt,
		CommandOption_WriteUInt,
		CommandOption_WritePair,
		CommandOption_WriteArray,
	};


	//
	// COMMAND
	// 
	
	// command category (subjective)
	enum CommandType_t : uint16_t
	{
		QCI_BASE,
		QCI_BONE,
		QCI_MODEL,
		QCI_TEXTURE,
		QCI_LOD,
		QCI_ANIM,
		QCI_BOX,
		QCI_COLLISON,

		QCI_COUNT,
	};

	constexpr const char* const s_CommandTypeNames[QCI_COUNT] =
	{
		nullptr,
		"_bone",
		nullptr,
		nullptr,
		"_lod",
		"_anim",
		"_box",
		"_collision",
	};

	typedef const uint32_t (*CommandTypeSortFunc_t)(const Command_t* const, const Command_t** const, const size_t, const CommandType_t);
	const uint32_t CommandSortDefault(const Command_t* const commands, const Command_t** const sorted, const size_t numCommands, const CommandType_t type);
	const uint32_t CommandSortModel(const Command_t* const commands, const Command_t** const sorted, const size_t numCommands, const CommandType_t type);
	const uint32_t CommandSortLOD(const Command_t* const commands, const Command_t** const sorted, const size_t numCommands, const CommandType_t type);
	const uint32_t CommandSortBox(const Command_t* const commands, const Command_t** const sorted, const size_t numCommands, const CommandType_t type);

	static CommandTypeSortFunc_t s_CommandTypeSortFunc[QCI_COUNT] =
	{
		&CommandSortDefault,
		&CommandSortDefault,
		&CommandSortModel,
		&CommandSortDefault,
		&CommandSortLOD,
		&CommandSortDefault,
		&CommandSortBox,
		&CommandSortDefault,
	};

	// command enum/index
	// changing sorting here can affect writing!
	enum CommandList_t : int16_t
	{
		// misc
		QC_MODELNAME,
		QC_STATICPROP,
		QC_OPAQUE,
		QC_MOSTLYOPAQUE,
		QC_OBSOLETE,
		QC_NOFORCEDFADE,
		QC_FORCEPHONEMECROSSFADE,
		QC_CONSTANTDIRECTIONALLIGHT,
		QC_AMBIENTBOOST,
		QC_DONOTCASTSHADOWS,
		QC_CASTTEXTURESHADOWS,
		QC_SUBD,
		QC_FADEDISTANCE,
		QC_ILLUMPOSITION,
		QC_EYEPOSITION,
		QC_MAXEYEDEFLECTION,
		QC_AUTOCENTER,

		QC_KEYVALUES,

		QC_INCLUDE,

		// models
		QC_MAXVERTS,
		QC_USEDETAILEDWEIGHTS,
		QC_USEVERTEXCOLOR,
		QC_USEEXTRATEXCOORD,
		QC_BODY,
		QC_BODYGROUP,
		QC_MODEL,

		QC_LOD,
		QC_SHADOWLOD,
		QC_ALLOWROOTLODS,
		QC_MINLOD,

		// materials
		QC_CDMATERIALS,
		QC_TEXTUREGROUP,
		QC_RENAMEMATERIAL,

		// bones
		QC_DEFINEBONE,
		QC_JOINTCONTENTS,
		QC_JOINTSURFACEPROP,
		QC_BONEMERGE,
		QC_SCREENALIGN,
		QC_LIMITROTATION,
		QC_BONESAVEFRAME,
		QC_JIGGLEBONE,
		QC_ATTACHMENT,

		// animation
		QC_POSEPARAMETER,
		QC_IKCHAIN,
		QC_IKAUTOPLAYLOCK,
		QC_INCLUDEMODEL,
		QC_SECTIONFRAMES,
		QC_ANIMBLOCKSIZE,
		QC_DEFAULTWEIGHTLIST,
		QC_WEIGHTLIST,
		QC_ANIMATION,
		QC_DECLAREANIM,
		QC_SEQUENCE,
		QC_DECLARESEQ,

		// bounds
		QC_BBOX,
		QC_CBOX,
		QC_HBOXSET,
		QC_HBOX,
		QC_HGROUP,
		QC_SKIPBONEINBBOX,

		// collision
		QC_CONTENTS,
		QC_SURFACEPROP,
		QC_COLLISIONMODEL,
		QC_COLLISIONJOINTS,
		QC_COLLISIONTEXT,

		// 
		QC_COMMANDCOUNT,

		QC_CMDBASE = 0,
	};

	// !!!!!!! THIS SHOULD NOT RETURN BUFFER SINCE USING CMDBUF from QC FILE!! RETURN SIZE (SANS NULL TERMINATOR)!
	typedef size_t(*CommandWriteFunc_t)(const Command_t* const, char*, size_t); // write this command to a text buffer
	typedef CommandOption_t*(*CommandParseBinaryFunc_t)(QCFile* const, uint32_t* const, const void* const, const uint32_t, const bool); // read this command from a binary buffer
	typedef CommandOption_t*(*CommandParseTextFunc_t)(QCFile* const, uint32_t* const, const char* const); // read this command from a text buffer

	struct CommandInfo_t
	{
		CommandInfo_t(const CommandList_t cmdId, const char* const cmdName, const CommandType_t cmdType, CommandWriteFunc_t funcWrite = nullptr, CommandParseBinaryFunc_t funcParseBinary = nullptr, CommandParseTextFunc_t funcParseText = nullptr,
			const Version_t min = s_QCVersion_MIN, const Version_t max = s_QCVersion_MAX) : name(cmdName), id(cmdId), type(cmdType), versionMin(min), versionMax(max), writeFunc(funcWrite), parseBinaryFunc(funcParseBinary), parseTextFunc(funcParseText) {}

		const char* const name;
		const CommandList_t id;
		const CommandType_t type;

		const Version_t versionMin;
		const Version_t versionMax;
		inline const bool VersionSupported(const Version_t version) const
		{
			return Version_t::VersionSupported(versionMin, versionMax, version.major, version.minor);
		}

		CommandWriteFunc_t writeFunc;
		CommandParseBinaryFunc_t parseBinaryFunc;
		CommandParseTextFunc_t parseTextFunc;
	};

	// generic commands
	size_t CommandGeneric_Write(const Command_t* const command, char* buffer, size_t bufferSize);
	CommandOption_t* CommandNone_ParseBinary(QCFile* const file, uint32_t* const numOptions, const void* const data, const uint32_t count, const bool store);
	CommandOption_t* CommandString_ParseBinary(QCFile* const file, uint32_t* const numOptions, const void* const data, const uint32_t count, const bool store);
	CommandOption_t* CommandFloat_ParseBinary(QCFile* const file, uint32_t* const numOptions, const void* const data, const uint32_t count, const bool store);
	//CommandOption_t* CommandChar_ParseBinary(QCFile* const file, uint32_t* const numOptions, const void* const data, const uint32_t count, const bool store);
	CommandOption_t* CommandByte_ParseBinary(QCFile* const file, uint32_t* const numOptions, const void* const data, const uint32_t count, const bool store);
	CommandOption_t* CommandInt_ParseBinary(QCFile* const file, uint32_t* const numOptions, const void* const data, const uint32_t count, const bool store);
	//CommandOption_t* CommandUInt_ParseBinary(QCFile* const file, uint32_t* const numOptions, const void* const data, const uint32_t count, const bool store);
	CommandOption_t* CommandPair_ParseBinary(QCFile* const file, uint32_t* const numOptions, const void* const data, const uint32_t count, const bool store);

	// custom command storage types
	struct IllumPositionData_t
	{
		IllumPositionData_t(const Vector* const pos, const int attach, const char* const bone = nullptr, const matrix3x4_t* const matrix = nullptr) : illumposition(pos), illumpositionattachmentindex(attach), localbone(bone), localmatrix(matrix)
		{
#ifdef _DEBUG
			if (useAttachment(illumpositionattachmentindex) && matrix == nullptr)
				assertm(false, "missing required data");
#endif // _DEBUG
		}

		const Vector* illumposition;
		int illumpositionattachmentindex;

		const char* localbone;
		const matrix3x4_t* localmatrix;

		static inline const bool useAttachment(const int idx) { return idx > 0 ? true : false; }
		inline const bool useAttachment() const { return useAttachment(illumpositionattachmentindex); }
	};

	struct BoneData_t
	{
		BoneData_t(const char* const boneName, const char* const boneParent/*, const char* const boneSurface*/, const int boneContents, const int boneFlags, const Vector* const pos, const RadianEuler* const rot, const matrix3x4_t* const fixup = nullptr) : name(boneName), parent(boneParent), /*surfaceprop(boneSurface),*/
			contents(boneContents), flags(boneFlags), position(pos), rotation(rot), fixupMatrix(fixup) { }

		const char* name;
		const char* parent;
		//const char* surfaceprop;

		const Vector* position;
		const RadianEuler* rotation;

		const matrix3x4_t* fixupMatrix;

		const int contents;
		const int flags;

		enum ScreenAlign_t
		{
			SCREENALIGN_SPHERE,
			SCREENALIGN_CYLINDER,
			SCREENALIGN_NONE,

			SCREENALIGN_COUNT,
		};

		enum BoneSaveFrame_t
		{
			BONESAVE_POS,
			BONESAVE_ROT,

			BONESAVE_COUNT,
		};

		enum BoneFlags_t : int
		{
			QCB_SCREEN_ALIGN_SPHERE		= 0x8,	// bone aligns to the screen, not constrained in motion.
			QCB_SCREEN_ALIGN_CYLINDER	= 0x10,	// bone aligns to the screen, constrained by it's own axis.

			QCB_HAS_SAVEFRAME_POS		= 0x200000,	// Vector48
			QCB_HAS_SAVEFRAME_ROT64		= 0x400000,	// Quaternion64
			QCB_HAS_SAVEFRAME_ROT32		= 0x800000,	// Quaternion32
		};

		inline const ScreenAlign_t ScreenAlignShape() const
		{
			if (flags & QCB_SCREEN_ALIGN_SPHERE)
			{
				return SCREENALIGN_SPHERE;
			}

			if (flags & QCB_SCREEN_ALIGN_CYLINDER)
			{
				return SCREENALIGN_CYLINDER;
			}

			assertm(false, "invalid screen align shape");
			return SCREENALIGN_NONE;
		}

		inline const bool UseFixups() const { return fixupMatrix ? true : false; }
	};

	constexpr const char* const s_ScreenAlignType[BoneData_t::SCREENALIGN_COUNT] =
	{
		"sphere",
		"cylinder",
		"",
	};

	constexpr const char* const s_BoneSaveFrameType[BoneData_t::BONESAVE_COUNT] =
	{
		"position",
		"rotation",
	};

	struct JiggleData_t
	{
		JiggleData_t(const char* boneName, const uint16_t jiggleFlags)
		{
			memset(this, 0, sizeof(JiggleData_t));
			bone = boneName;
			flags = jiggleFlags;
		}

		enum Flags_t : uint16_t
		{
			IS_FLEXIBLE				= 0x01,
			IS_RIGID				= 0x02,
			HAS_FLEX				= 0x02,
			HAS_YAW_CONSTRAINT		= 0x04,
			HAS_PITCH_CONSTRAINT	= 0x08,
			HAS_ANGLE_CONSTRAINT	= 0x10,
			HAS_LENGTH_CONSTRAINT	= 0x20,
			HAS_BASE_SPRING			= 0x40,
			IS_BOING				= 0x80,

			// for apex
			_IS_FLEXIBLE			= 0x1000, // IS_FLEXIBLE << 12
			_HAS_FLEX				= 0x2000, // HAS_FLEX << 12
			_IS_APEX				= 0x8000,
		};

		const char* bone;
		uint16_t flags;

		// general params
		float length; // how far from bone base, along bone, is tip
		float tipMass;
		float tipFriction; // friction applied to tip velocity, 0-1

		// flexible params
		float yawStiffness;
		float yawDamping;
		float pitchStiffness;
		float pitchDamping;
		float alongStiffness;
		float alongDamping;

		// angle constraint
		float angleLimit; // maximum deflection of tip in radians

		// yaw constraint
		float minYaw; // in radians
		float maxYaw; // in radians
		float yawFriction;
		float yawBounce;

		// pitch constraint
		float minPitch; // in radians
		float maxPitch; // in radians
		float pitchFriction;
		float pitchBounce;

		// base spring
		float baseMass;
		float baseStiffness;
		float baseDamping;
		float baseMinLeft;
		float baseMaxLeft;
		float baseLeftFriction;
		float baseMinUp;
		float baseMaxUp;
		float baseUpFriction;
		float baseMinForward;
		float baseMaxForward;
		float baseForwardFriction;

		// boing
		float boingImpactSpeed;
		float boingImpactAngle;
		float boingDampingRate;
		float boingFrequency;
		float boingAmplitude;

		// flag funcs
		inline void FixFlagsApex()
		{
			constexpr uint16_t maskTipFlex = static_cast<uint16_t>(IS_FLEXIBLE | HAS_FLEX);
			constexpr uint16_t maskExcludeFlags = static_cast<uint16_t>(~maskTipFlex);

			const uint16_t flagsOld = flags;

			flags = flagsOld & maskExcludeFlags;

			flags |= (_IS_APEX | ((flagsOld & maskTipFlex) << 12)); // mark this as an apex jigglebone and store the discarded flags

			if (flagsOld & HAS_FLEX)
			{
				if (flagsOld & IS_FLEXIBLE)
				{
					flags |= IS_FLEXIBLE;
					return;
				}

				flags |= IS_RIGID;
			}
		}

		/*inline void FixFlagsApex()
		{
			constexpr uint16_t maskTipFlex = static_cast<uint16_t>(IS_FLEXIBLE | HAS_FLEX);
			constexpr uint16_t maskExcludeFlags = static_cast<uint16_t>(~maskTipFlex);

			const uint16_t flagsOld = flags;

			flags = flagsOld & maskExcludeFlags;

			flags |= (_IS_APEX | ((flagsOld & maskTipFlex) << 12)); // mark this as an apex jigglebone and store the discarded flags

			if (flagsOld & IS_FLEXIBLE)
			{
				flags |= IS_FLEXIBLE;
				return;
			}

			flags |= IS_RIGID;
		}*/

		const bool IsFlexible() const { return flags & IS_FLEXIBLE; }
		const bool IsRigid() const { return flags & IS_RIGID; }
		const bool IsBoing() const { return flags & IS_BOING; }
		const bool HasBaseSpring() const { return flags & HAS_BASE_SPRING; }

		const bool HasYawConstraint() const { return flags & HAS_YAW_CONSTRAINT; }
		const bool HasPitchConstraint() const { return flags & HAS_PITCH_CONSTRAINT; }
		const bool HasAngleConstraint() const { return flags & HAS_ANGLE_CONSTRAINT; }
		const bool HasLengthConstraint() const { return flags & HAS_LENGTH_CONSTRAINT; }

		const bool HasNoFlex() const { return (flags & _IS_APEX) && !(flags & _HAS_FLEX); }

		const uint32_t CommonJiggleOptionCount() const
		{
			uint32_t numOptions = 3u; // length, tip_mass, tip_friction

			numOptions += HasAngleConstraint();

			// while friction and bounce don't set the JIGGLE_HAS_YAW_CONSTRAINT flag, they don't get used unless it is set
			if (HasYawConstraint())
			{
				numOptions += 3u; // yaw_constraint, yaw_friction, yaw_bounce
			}

			// while friction and bounce don't set the JIGGLE_HAS_PITCH_CONSTRAINT flag, they don't get used unless it is set
			if (HasPitchConstraint())
			{
				numOptions += 3u; // pitch_constraint, pitch_friction, pitch_bounce
			}

			// apex only
			if (HasNoFlex())
			{
				numOptions++;
			}

			return numOptions;
		}

		inline const uint32_t FlexibleOptionCount() const
		{
			uint32_t numOptions = 6; // yaw_stiffness, yaw_damping, pitch_stiffness, pitch_damping, along_stiffness, along_damping

			const bool allowLengthFlex = !HasLengthConstraint();
			numOptions += allowLengthFlex;

			return numOptions;
		}

		constexpr uint32_t BaseSpringOptionCount() const
		{
			return 9u;
		}

		constexpr uint32_t BoingOptionCount() const
		{
			return 5u;
		}

		// setup data
		inline void SetGeneral(const float len, const float mass, const float friction = 0.0f)
		{
			length = len;
			tipMass = mass;
			tipFriction = friction;
		}

		inline void SetFlexible(const float yStiffness, const float yDamping, const float pStiffness, const float pDamping, const float aStiffness, const float aDamping)
		{
			yawStiffness = yStiffness;
			yawDamping = yDamping;
			pitchStiffness = pStiffness;
			pitchDamping = pDamping;
			alongStiffness = aStiffness;
			alongDamping = aDamping;
		}

		inline void SetAngleConstraint(const float limit)
		{
			angleLimit = limit;
		}

		inline void SetYawConstraint(const float min, const float max, const float friction, const float bounce)
		{
			minYaw = min;
			maxYaw = max;
			yawFriction = friction;
			yawBounce = bounce;
		}

		inline void SetPitchConstraint(const float min, const float max, const float friction, const float bounce)
		{
			minPitch = min;
			maxPitch = max;
			pitchFriction = friction;
			pitchBounce = bounce;
		}

		inline void SetBaseSpring(const float mass, const float stiffness, const float damping, const float minLeft, const float maxLeft, const float leftFriction, const float minUp, const float maxUp, const float upFriction,
			const float minForward, const float maxForward, const float forwardFriction)
		{
			baseMass = mass;
			baseStiffness = stiffness;
			baseDamping = damping;
			baseMinLeft = minLeft;
			baseMaxLeft = maxLeft;
			baseLeftFriction = leftFriction;
			baseMinUp = minUp;
			baseMaxUp = maxUp;
			baseUpFriction = upFriction;
			baseMinForward = minForward;
			baseMaxForward = maxForward;
			baseForwardFriction = forwardFriction;
		}

		inline void SetBoing(const float impactSpeed, const float impactAngle, const float dampingRate, const float frequency, const float amplitude)
		{
			boingImpactSpeed = impactSpeed;
			boingImpactAngle = impactAngle;
			boingDampingRate = dampingRate;
			boingFrequency = frequency;
			boingAmplitude = amplitude;
		}
	};

	struct AttachmentData_t
	{
		AttachmentData_t(const char* const attachName, const char* const attachBone, const uint32_t attachFlags, const matrix3x4_t* const attachMatrix) : name(attachName), flags(attachFlags), localbone(attachBone), localmatrix(attachMatrix) {}

		enum Type_t
		{
			ATTACH_ABSOLUTE,
			ATTACH_RIGID,
			ATTACH_ROTATE,
			ATTACH_WORLD_ALIGN,
			ATTACH_X_AND_Z_AXES,

			ATTACH_TYPE_COUNT,
		};

		const char* name;
		uint32_t flags;

		const char* localbone;
		const matrix3x4_t* localmatrix;

		// attachment requires ATTACHMENT_FLAG_WORLD_ALIGN
#ifndef ATTACHMENT_FLAG_WORLD_ALIGN
		static_assert(false);
		inline const bool WorldAlign() const { return false; }
#else
		inline const bool WorldAlign() const { return flags & ATTACHMENT_FLAG_WORLD_ALIGN ? true : false; }
#endif
	};

	struct BodyGroupData_t
	{
		BodyGroupData_t(const char* const groupName, const uint32_t partCount) : name(groupName), numParts(partCount)
		{
			parts = new const char*[numParts]{};
		}

		~BodyGroupData_t()
		{
			FreeAllocArray(parts);
		}

		const char* name;
		const char** parts;
		uint32_t numParts;

		inline void SetPart(const uint32_t idx, const char* const partName) { parts[idx] = partName; }
		inline void SetPart(QCFile* const qc, const uint32_t idx, const char* const partName)
		{
			const char* const saved = qc->WriteString(partName);
			SetPart(idx, saved);
		}

		inline void SetBlank(const uint32_t idx) { SetPart(idx, nullptr); }
	};

	struct TextureGroupData_t
	{
		TextureGroupData_t(const char** skinMaterials, const int16_t* skinGroups, const int16_t* skinIndices, const int skinRef, const int skinFamilies, const uint32_t skinIndex, const char** skinNames = nullptr) : materials(skinMaterials), skins(skinGroups), indices(skinIndices),
			names(skinNames), numSkinRef(skinRef), numSkinFamilies(skinFamilies), numIndices(skinIndex) {
		}

		const char** materials;
		const int16_t* skins; // skingroups
		const int16_t* indices; // indices of textures to write

		const char** names;

		int numSkinRef;
		int numSkinFamilies;
		uint32_t numIndices;

		inline const bool HasNames() const { return names ? true : false; }
	};

	struct LodData_t
	{
		LodData_t(const uint32_t modelCount, const uint32_t boneCount, const uint32_t materialCount, const float switchPoint, bool shadowlod) : numModels(modelCount), numBoneSlots(boneCount), numBoneUsed(0u), numReplacementMaterials(materialCount),
			threshold(switchPoint), isShadowLOD(shadowlod), useShadowLODMaterials(false), modelBaseNames(nullptr), modelReplaceNames(nullptr), boneBaseNames(nullptr), boneReplaceNames(nullptr), materialBaseNames(nullptr), materialReplaceNames(nullptr)
		{
			alloc = new const char* [(numModels * 2) + (numBoneSlots * 2) + (numReplacementMaterials * 2)] {};

			modelBaseNames = alloc;
			modelReplaceNames = modelBaseNames + numModels;

			boneBaseNames = modelReplaceNames + numModels;
			boneReplaceNames = boneBaseNames + numBoneSlots;

			if (numReplacementMaterials > 0)
			{
				materialBaseNames = boneReplaceNames + numBoneSlots;
				materialReplaceNames = materialBaseNames + numReplacementMaterials;
			}
		}

		~LodData_t()
		{
			FreeAllocArray(alloc);
		}

		const char** alloc;

		const char** modelBaseNames;
		const char** modelReplaceNames;
		uint32_t numModels;

		uint32_t numBoneSlots;
		const char** boneBaseNames;
		const char** boneReplaceNames;
		uint16_t numBoneUsed;

		bool isShadowLOD;
		bool useShadowLODMaterials;

		uint32_t numReplacementMaterials;
		const char** materialBaseNames;
		const char** materialReplaceNames;

		float threshold;

		inline void ReplaceModel(const uint32_t idx, const char* const baseName, const char* const replaceName)
		{ 
			modelBaseNames[idx] = baseName;
			modelReplaceNames[idx] = replaceName;
		}

		inline void ReplaceModel(QCFile* const qc, const uint32_t idx, const char* const baseName, const char* const replaceName)
		{
			const char* const base = qc->WriteString(baseName);
			const char* const replace = qc->WriteString(replaceName);
			ReplaceModel(idx, base, replace);
		}

		inline void RemoveModel(const uint32_t idx, const char* const baseName) { ReplaceModel(idx, baseName, nullptr); }
		inline void RemoveModel(QCFile* const qc, const uint32_t idx, const char* const baseName)
		{
			const char* const base = qc->WriteString(baseName);
			RemoveModel(idx, base);
		}

		inline void IgnoreModel(const uint32_t idx) { ReplaceModel(idx, nullptr, nullptr); }

		inline void ReplaceBone(const uint32_t idx, const char* const baseName, const char* const replaceName)
		{
			boneBaseNames[idx] = baseName;
			boneReplaceNames[idx] = replaceName;

			numBoneUsed++;
		}

		inline void ReplaceBone(QCFile* const qc, const uint32_t idx, const char* const baseName, const char* const replaceName)
		{
			const char* const base = qc->WriteString(baseName);
			const char* const replace = qc->WriteString(replaceName);
			ReplaceBone(idx, base, replace);
		}

		inline void ReplaceMaterial(const uint32_t idx, const char* const baseName, const char* const replaceName)
		{
			materialBaseNames[idx] = baseName;
			materialReplaceNames[idx] = replaceName;
		}

		inline void ReplaceMaterial(QCFile* const qc, const uint32_t idx, const char* const baseName, const char* const replaceName)
		{
			const char* const base = qc->WriteString(baseName);
			const char* const replace = qc->WriteString(replaceName);
			ReplaceMaterial(idx, base, replace);
		}

		inline const bool UseThreshold() const { return isShadowLOD ? false : true; }
	};

	struct PoseParamData_t
	{
		PoseParamData_t(const char* const poseName, const int poseFlags, const float poseStart, const float poseEnd, const float poseLoop) : name(poseName), flags(poseFlags), start(poseStart), end(poseEnd), loop(poseLoop) {}

		const char* name;

		int flags;
		float start;
		float end;
		float loop;

		// poseparm requires STUDIO_LOOPING
#ifndef STUDIO_LOOPING
		static_assert(false);
		inline const bool IsLooped() const { return false; }
#else
		inline const bool IsLooped() const { return flags & STUDIO_LOOPING ? true : false; }
#endif
		inline const bool IsWrapped() const { return end - start == loop; }
	};

	struct IKChainData_t
	{
		IKChainData_t(const char* const chainName, const char* const chainBone, const Vector* const chainKnee, const float chainAngle = 0.0f) : name(chainName), bone(chainBone), knee(chainKnee), angleUnknown(chainAngle) {}

		const char* name;
		const char* bone; // the foot bone, typically stored in link index 2
		const Vector* knee; // stored in the thigh link for some reason ? (index 0)

		float angleUnknown; // cosine of a degree angle

		inline const bool UseKnee() const { return (knee->x != 0.0f) || (knee->y != 0.0f) || (knee->z != 0.0f); }
	};

	struct IKLockData_t
	{
		IKLockData_t(const char* const lockChain, const float lockPos, const float lockQ) : chain(lockChain), flPosWeight(lockPos), flLocalQWeight(lockQ) {}

		const char* chain;

		float flPosWeight;
		float flLocalQWeight;
	};

	constexpr const char* const s_AttachmentTypeNames[AttachmentData_t::Type_t::ATTACH_TYPE_COUNT] =
	{
		"absolute",
		"rigid",
		"rotate",
		"world_align",
		"x_and_z_axes",
	};

	struct Hitbox_t
	{
		// source/reSource
		Hitbox_t(const char* const boxBone, const int boxGroup, const Vector* const boxMin, const Vector* const boxMax, const char* const boxName, const char* const boxHitDataGroup = nullptr, const int boxForceCrit = 0) :
			bone(boxBone), group(boxGroup), bbmin(boxMin), bbmax(boxMax), name(boxName), hitDataGroup(boxHitDataGroup), forceCritPoint(boxForceCrit), flRadius(0.0f), bbangle(nullptr) { }
		// csgo
		Hitbox_t(const char* const boxBone, const int boxGroup, const Vector* const boxMin, const Vector* const boxMax, const char* const boxName, const float boxRadius = 0.0f, const QAngle* const boxAngle = nullptr) :
			bone(boxBone), group(boxGroup), bbmin(boxMin), bbmax(boxMax), name(boxName), hitDataGroup(nullptr), forceCritPoint(0), flRadius(boxRadius), bbangle(boxAngle) {}

		const char* bone;
		int group;

		const Vector* bbmin;
		const Vector* bbmax;
		const char* name;
		inline const bool HasName() const { return strncmp(name, "", 1) == 0 ? false : true; } // bleehh

		// reSource
		const char* hitDataGroup;
		int forceCritPoint;
		inline const bool HasHitDataGroup() const
		{
			if (hitDataGroup == nullptr)
				return false;

			return strncmp(hitDataGroup, "", 1) == 0 ? false : true;
		}

		// csgo
		float flRadius;
		const QAngle* bbangle; // "boundingBoxPitchYawRoll" (crowbar)

		inline const bool IsCSGO() const { return bbangle ? true : false; }
	};

#ifdef HAS_PHYSICSMODEL_PARSER
	struct PhysicsData_t
	{
		PhysicsData_t(const char* const name, const PhysicsModel::CParsedPhys* const phys) : filename(name), parsed(phys) {}

		enum AnimFricOrder_t
		{
			ANIMFRIC_TIMEIN,
			ANIMFRIC_TIMEOUT,
			ANIMFRIC_TIMEHOLD,
			ANIMFRIC_MIN,
			ANIMFRIC_MAX,

			ANIMFRIC_COUNT,
		};

		enum JointConType_t
		{
			JOINCON_FREE,
			JOINCON_FIXED,
			JOINCON_LIMIT,

			JOINTCON_COUNT,
		};

		const char* filename;
		const PhysicsModel::CParsedPhys* parsed;

		const JointConType_t TypeFromJointConstraint(const PhysicsModel::RagdollConstraint::Constraint_t* const constraint) const
		{
			using namespace PhysicsModel;

			const float min = constraint->min;
			const float max = constraint->max;
			const float fric = constraint->friction;

			if (min == -360.0f && max == 360.0f)
			{
				return JOINCON_FREE;
			}

			if (min == 0.0f && max == 0.0f && fric == 0.0f)
			{
				return JOINCON_FIXED;
			}

			return JOINCON_LIMIT;
		}

		// get the rough amount of required options, with excess
		const uint32_t EstimateOptionCount() const
		{
			// basic allows for all the non jointed options, and jointed options
			uint32_t numOptions = 24u + (8u * parsed->GetSolidCount());

			const PhysicsModel::EditParam* const editParam = parsed->GetEditParam();
			if (editParam)
			{
				numOptions += editParam->GetJointMergeCount();
			}

			const PhysicsModel::CollisionRule* const collisionRule = parsed->GetCollisionRule();
			if (collisionRule)
			{
				numOptions += collisionRule->GetCollisionPairCount();
			}

			return numOptions;
		}
	};

	constexpr const char* const s_CollModel_JointConstraintAxis[PhysicsModel::RagdollConstraint::CONAXIS_COUNT]
	{
		"x",
		"y",
		"z",
	};

	constexpr const char* const s_CollModel_JointConstraintType[PhysicsModel::RagdollConstraint::CONAXIS_COUNT]
	{
		"free",
		"fixed",
		"limit",
	};
#endif // HAS_PHYSICSMODEL_PARSER

	constexpr int s_SectionFrames_DefaultFrameCount = 30;
	constexpr int s_SectionFrames_DefaultMinFrame = 120;
	struct SectionFrameData_t
	{
		SectionFrameData_t(const int sectionFrames, const int minFrames) : sectionFrameCount(sectionFrames), minFramesForSections(minFrames) {}

		int sectionFrameCount;
		int minFramesForSections;
	};

	struct AnimBlockData_t
	{
		AnimBlockData_t(const int blockSize, const int zeroFrames, const bool useNoStall, const bool useHighRes, const bool useLowRes, const bool useCacheHighRes) : maxBlockSizeInBytes(blockSize), numFrames(zeroFrames), noStall(useNoStall),
			highRes(useHighRes), lowRes(useLowRes), cacheHighRes(useCacheHighRes) {}

		int maxBlockSizeInBytes; // not kilobytes
		int numFrames;
		bool noStall;
		bool highRes;
		bool lowRes;
		bool cacheHighRes;

		inline const bool UseNumFrames() const { return (numFrames == 3 || noStall) ? false : true; }

		inline const uint32_t EstimateOptionCount() const
		{
			uint32_t numOptions = 1u; // datasize

			numOptions += noStall;
			numOptions += highRes;
			numOptions += lowRes;
			numOptions += UseNumFrames();
			numOptions += cacheHighRes;

			return numOptions;
		}
	};

	struct WeightListData_t
	{
		WeightListData_t(const char* listName, const char** const listBones, const float* const listWeights, const int listLength, const bool setDefault) : name(listName), bones(listBones), weights(listWeights), numWeights(listLength), isDefault(setDefault) {}

		const char* name;
		const char** bones;
		const float* weights;
		int numWeights;
		const bool isDefault;
	};

	constexpr float s_Animation_DefaultFPS = 30.0f;
	struct AnimationOptions_t
	{
		AnimationOptions_t(const uint32_t animOptions, const uint32_t animFeatures) : flags(animOptions), features(static_cast<FeatureSet_t>(animFeatures)) {}

		enum FeatureSet_t : uint32_t
		{
			ANIM_OPT_FEATURE_INVALID,
			ANIM_OPT_FEATURE_ANIM,
			ANIM_OPT_FEATURE_SEQ,
			ANIM_OPT_FEATURE_ALL,
		};

		enum Options_t : uint32_t
		{
			ANIM_OPTION_LOOPING			= 0x1,
			ANIM_OPTION_SNAP			= 0x2,
			ANIM_OPTION_DELTA			= 0x4,		
			ANIM_OPTION_AUTOPLAY		= 0x8,
			ANIM_OPTION_POST			= 0x10,
			ANIM_OPTION_NOANIM			= 0x20,
			ANIM_OPTION_CYCLEPOSE		= 0x80,
			ANIM_OPTION_REALTIME		= 0x100,
			ANIM_OPTION_HIDDEN			= 0x400,

			ANIM_OPTION_WORLDSPACE		= 0x4000,
			ANIM_OPTION_NOFORCELOOP		= 0x8000,	// do not force the animation loop

			ANIM_OPTION_ROOTMOTION		= 0x40000,	// uses rootmotion style animation track
			ANIM_OPTION_DEFAULTPOSE		= 0x100000,	// 0x20000 HAS_ANIM flag from apex, if 'noanimation' or 'noanim_keepduration' (STUDIO_ALLZEROS) is used, it will yield different results (goes down different pose path, does not get default bone data)
			ANIM_OPTION_SUPPGEST		= 0x200000,

			ANIM_OPTION_AUTOIK			= 0x1000000,
			ANIM_OPTION_NOAUTOIK		= 0x2000000,
		};

		uint32_t flags;
		FeatureSet_t features;

		inline void SetLooping() { flags |= ANIM_OPTION_LOOPING; }
		inline void SetSnap() { flags |= ANIM_OPTION_SNAP; }
		inline void SetDelta() { flags |= ANIM_OPTION_DELTA; }
		inline void SetAutoPlay() { flags |= ANIM_OPTION_AUTOPLAY; }
		inline void SetPost() { flags |= ANIM_OPTION_POST; }
		inline void SetNoAnim() { flags |= ANIM_OPTION_NOANIM; }
		inline void SetCyclePose() { flags |= ANIM_OPTION_CYCLEPOSE; }
		inline void SetRealTime() { flags |= ANIM_OPTION_REALTIME; }
		inline void SetHidden() { flags |= ANIM_OPTION_HIDDEN; }
		inline void SetWorldSpace() { flags |= ANIM_OPTION_WORLDSPACE; }
		inline void SetNoForceLoop() { flags |= ANIM_OPTION_NOFORCELOOP; }

		inline void SetRootMotion() { flags |= ANIM_OPTION_ROOTMOTION; }
		inline void SetDefaultPose() { flags |= ANIM_OPTION_DEFAULTPOSE; }
		inline void SetSuppressGestures() { flags |= ANIM_OPTION_SUPPGEST; }

		inline void SetAutoIK(const bool autoIk) { flags |= autoIk ? ANIM_OPTION_AUTOIK : ANIM_OPTION_NOAUTOIK; }

		inline const bool IsLooping() const { return flags & ANIM_OPTION_LOOPING; }
		inline const bool HasSnap() const { return flags & ANIM_OPTION_SNAP; }
		inline const bool IsDelta() const { return flags & ANIM_OPTION_DELTA; }
		inline const bool IsAutoPlay() const { return flags & ANIM_OPTION_AUTOPLAY; }
		inline const bool HasPost() const { return flags & ANIM_OPTION_POST; }
		inline const bool HasNoAnim() const { return flags & ANIM_OPTION_NOANIM; }
		inline const bool HasCyclePose() const { return flags & ANIM_OPTION_CYCLEPOSE; }
		inline const bool IsRealTime() const { return flags & ANIM_OPTION_REALTIME; }
		inline const bool IsHidden() const { return flags & ANIM_OPTION_HIDDEN; }
		inline const bool IsWorldSpace() const { return flags & ANIM_OPTION_WORLDSPACE; }
		inline const bool HasNoForceLoop() const { return flags & ANIM_OPTION_NOFORCELOOP; }

		inline const bool HasRootMotion() const { return flags & ANIM_OPTION_ROOTMOTION; }
		inline const bool UseDefaultPose() const { return flags & ANIM_OPTION_DEFAULTPOSE; }
		inline const bool SuppressGestures() const { return flags & ANIM_OPTION_SUPPGEST; }

		inline const bool UseAutoIK() const { return flags & ANIM_OPTION_AUTOIK; }
		inline const bool UseNoAutoIK() const { return flags & ANIM_OPTION_NOAUTOIK; }

		const uint32_t GetUsedOptions() const
		{
			constexpr uint32_t maskSequence = ~(ANIM_OPTION_NOANIM | ANIM_OPTION_ROOTMOTION | ANIM_OPTION_DEFAULTPOSE | ANIM_OPTION_AUTOIK | ANIM_OPTION_NOAUTOIK);
			constexpr uint32_t maskAnimation = ~(ANIM_OPTION_WORLDSPACE);

			switch (features)
			{
			case ANIM_OPT_FEATURE_ANIM:
			{
				return __popcnt(flags & maskAnimation);
			}
			case ANIM_OPT_FEATURE_SEQ:
			{
				return __popcnt(flags & maskSequence);
			}
			case ANIM_OPT_FEATURE_ALL:
			{
				return __popcnt(flags & ~ANIM_OPTION_DELTA) + (flags & ANIM_OPTION_DELTA ? 2u : 0u);
			}
			default:
			{
				assertm(false, "invalid features");
				return 0u;
			}
			}
		}
	};

	struct AnimationMotion_t
	{
		AnimationMotion_t() = default;
		AnimationMotion_t(const int end, const int flags) : endframe(end), motionflags(flags) {}

		// slightly cheaty, but these flags are sequential so
		enum MotionAxis_t
		{
			MOTION_X,
			MOTION_Y,	
			MOTION_Z,
			MOTION_XR,
			MOTION_YR,
			MOTION_ZR,

			MOTION_LX,
			MOTION_LY,
			MOTION_LZ,
			MOTION_LXR,
			MOTION_LYR,
			MOTION_LZR,

			MOTION_LINEAR,
			MOTION_QUADRATIC_MOTION, // tucked away in studiomdl.h

			MOTION_OPT_COUNT,
		};

		int endframe;
		int motionflags;

		inline const int AxisCount() const { return static_cast<int>(__popcnt(motionflags & STUDIO_TYPES)); }
	};

	constexpr const char* const s_AnimationMotionAxis[AnimationMotion_t::MOTION_OPT_COUNT] =
	{
		"X",
		"Y",
		"Z",
		"XR",
		"YR",
		"ZR",

		"LX",
		"LY",
		"LZ",
		"LXR",
		"LYR",
		"LZR",

		"LM",
		"LQ",
	};

	struct AnimationIKRule_t
	{
		AnimationIKRule_t() = default;
		AnimationIKRule_t(const int ruleType, const int ruleChain, const int ruleBone, const int ruleSlot, const char* const ruleBoneName, const char* const ruleChainName, const char* const ruleAttachement) : bonename(ruleBoneName), chainname(ruleChainName), attachment(ruleAttachement),
			type(ruleType), chain(ruleChain), bone(ruleBone), slot(ruleSlot), height(0.0f), radius(0.0f), floor(0.0f), pos(nullptr), q(nullptr), start(0.0f), peak(0.0f), tail(1.0f), end(1.0f), contact(0.0f), drop(0.0f), top(0.0f), endHeight(0.0f), isFixup(false) {}

		const char* bonename;
		const char* chainname;
		const char* attachment;

		bool isFixup;

		int type;
		int chain;
		int bone;
		int slot;

		float height;
		float radius;
		float floor;
		const Vector* pos;
		const Quaternion* q;

		float start;
		float peak;
		float tail;
		float end;

		float contact;
		float drop;
		float top;

		float endHeight; // new in v52

		// init per option (ish) because a long constructor is messy and prone to minor spelling mistakes
		inline void SetRange(const float ruleStart, const float rulePeak, const float ruleTail, const float ruleEnd)
		{
			start = ruleStart;
			peak = rulePeak;
			tail = ruleTail;
			end = ruleEnd;
		}

		inline void SetOrigin(const Vector* const rulePos, const Quaternion* const ruleQ)
		{
			pos = rulePos;
			q = ruleQ;
		}

		inline void SetFoot(const float ruleHeight, const float ruleRadius, const float ruleFloor, const float ruleEndHeight)
		{
			height = ruleHeight;
			radius = ruleRadius;
			floor = ruleFloor;
			endHeight = ruleEndHeight;
		}

		inline void SetFootContact(const float ruleContact, const float ruleDrop, const float ruleTop)
		{
			contact = ruleContact;
			drop = ruleDrop;
			top = ruleTop;
		}

		inline void MarkAsFixup() { isFixup = true; }
	};

	struct AnimationData_t
	{
		AnimationData_t(const char* const animName, const char* const animPath, const float animFPS, const int animFrames, const char* const animWeightList = nullptr) : name(animName), filepath(animPath), subtractPath(nullptr), weightlist(animWeightList),
			fps(animFPS), numFrames(animFrames), options(0u, 0u), motions(nullptr), numMotions(0), ikRules(nullptr), numIKRules(0) {}

		const char* name;
		const char* filepath;
		const char* subtractPath;
		const char* weightlist;

		float fps;
		int numFrames;

		AnimationOptions_t options;

		AnimationMotion_t* motions;
		int numMotions;

		int numIKRules;
		AnimationIKRule_t* ikRules;

		const uint32_t EstimateOptionCount_Anim() const
		{
			return numMotions + numIKRules + options.GetUsedOptions();
		}

		inline void SetOptions(const AnimationOptions_t animOptions) { options = animOptions; }
		inline void SetSubtractAnim(const char* const path) { subtractPath = path; }

		// motions
		inline void SetMotion(const int index, const int endframe, const int motionflags)
		{
			assertm(motions, "no valid motions");
			assertm(index < numMotions && index >= 0, "invalid motion index");

			motions[index] = AnimationMotion_t(endframe, motionflags);
		}

		inline void EnableMotion(QCFile* const file, const int motionCount)
		{
			numMotions = motionCount;
			motions = reinterpret_cast<AnimationMotion_t*>(file->ReserveData(sizeof(AnimationMotion_t) * numMotions));
		}

		void BuildAxisForMotion(const int index, const char** axis) const
		{
			const AnimationMotion_t* const motion = motions + index;

			for (int i = 0, idx = 0; i < AnimationMotion_t::MOTION_OPT_COUNT; i++)
			{
				// hack, relies on flags being in order
				const int flag = 1 << i;
				if ((motion->motionflags & flag) == false)
					continue;

				axis[idx] = s_AnimationMotionAxis[i];
				idx++;
			}
		}

		// ik rule
		inline void SetIKRule(const int index, const int type, const int chain, const int bone, const int slot, const char* const boneName, const char* const chainName, const char* const attachment)
		{
			assertm(ikRules, "no valid ik rules");
			assertm(index < numIKRules && index >= 0, "invalid ik rule index");

			ikRules[index] = AnimationIKRule_t(type, chain, bone, slot, boneName, chainName, attachment);
		}

		inline void EnableIKRules(QCFile* const file, const int ikRuleCount)
		{
			numIKRules = ikRuleCount;
			ikRules = reinterpret_cast<AnimationIKRule_t*>(file->ReserveData(sizeof(AnimationIKRule_t) * numIKRules));
		}
	};

	struct SequenceEvent_t
	{
		SequenceEvent_t() = default;
		SequenceEvent_t(const char* const eventName, const int eventId, const bool useNewStyle, const float eventCycle, const float eventUnk, const char* const eventOptions) : name(eventName), event(eventId), newEventStyle(useNewStyle), cycle(eventCycle),
			unk(eventUnk), options(eventOptions) {}

		const char* name;
		int event;

		bool newEventStyle;

		float cycle;
		float unk;

		const char* options;

		inline const bool UseEventName() const { return newEventStyle; }
		inline const bool HasOptions() const { return options[0] ? true : false; }
	};

	struct SequenceLayer_t
	{
		SequenceLayer_t() = default;
		SequenceLayer_t(const char* const layerSequence, const char* const layerPose, const int layerFlags, const float layerStart, const float layerPeak, const float layerTail, const float layerEnd) : sequence(layerSequence), pose(layerPose), flags(layerFlags),
			start(layerStart), peak(layerPeak), tail(layerTail), end(layerEnd) {}

		const char* sequence;
		const char* pose;

		int flags;
		float start;	// beginning of influence
		float peak;		// start of full influence
		float tail;		// end of full influence
		float end;		// end of all influence

		const bool UseBlendLayer() const
		{
			constexpr int mask = ~STUDIO_AL_LOCAL;

			if (flags & mask)
				return true;

			if (start != 0.0f || peak != 0.0f || tail != 0.0f || end != 0.0f)
				return true;

			return false;
		}
	};

	struct SequenceActMod_t
	{
		SequenceActMod_t() = default;
		SequenceActMod_t(const char* actName, const bool actNegated = false) : activity(actName), negate(actNegated) {};

		const char* activity;
		bool negate;
	};

	constexpr float s_Sequence_DefaultFadeIn = 0.2f;
	constexpr float s_Sequence_DefaultFadeOut = 0.2f;

#define SEQ_ADD_OPT(condition) (condition ? 1u : 0u)
	struct SequenceData_t : AnimationData_t
	{
		SequenceData_t(const char* const seqLabel, const char* const seqActivity, const int seqActWeight, const float animFPS, const int animFrames, const char* const animWeightList, const int blendWidth, const int blendHeight,
			const float seqFadeIn, const float seqFadeOut, const float seqEntryPhase, const float seqExitPhase, const char* const seqKVs, const int seqIkResetMask) : AnimationData_t(seqLabel, nullptr, animFPS, animFrames, animWeightList),
			activity(seqActivity), activityWeight(seqActWeight), events(nullptr), numEvents(0), blends(nullptr), numBlends(0), param(), paramstart(), paramend(), fadeintime(seqFadeIn), fadeouttime(seqFadeOut),
			entrynode(nullptr), exitnode(nullptr), localentrynode(0), localexitnode(0), nodeflags(0), exitphase(seqEntryPhase), entryphase(seqExitPhase), layers(nullptr), numLayers(0), ikLocks(nullptr), numIKLocks(0), keyValues(seqKVs), cyclePose(nullptr),
			actmods(nullptr), numActMods(0), ikResetMask(seqIkResetMask)
		{
			groupsize[0] = blendWidth;
			groupsize[1] = blendHeight;
		}

		SequenceData_t(QCFile* file, const char* const seqLabel, const char* const seqActivity, const int seqActWeight, const float animFPS, const int animFrames, const char* const animWeightList, const int blendWidth, const int blendHeight, const int blendCount,
			const float seqFadeIn, const float seqFadeOut, const float seqEntryPhase, const float seqExitPhase, const char* const seqKVs, const int seqIkResetMask) :
			SequenceData_t(seqLabel, seqActivity, seqActWeight, animFPS, animFrames, animWeightList, blendWidth, blendHeight, seqFadeIn, seqFadeOut, seqEntryPhase, seqExitPhase, seqKVs, seqIkResetMask)
		{
			EnableBlends(file, blendCount);
		}

		const char* activity;
		int activityWeight;
		inline const bool HasActivity() const { return activity && activity[0] != '\0'; }

		int numEvents;
		SequenceEvent_t* events;

		const char** blends;
		int numBlends;

		int groupsize[2]; // width x height
		inline const int& GetWidth() const { return groupsize[0]; }
		inline const int& GetHeight() const { return groupsize[1]; }

		const char* param[2];
		float paramstart[2];
		float paramend[2];

		float fadeintime;
		float fadeouttime;

		const char* entrynode;
		const char* exitnode;
		int localentrynode;
		int localexitnode;
		int nodeflags;

		float entryphase;
		float exitphase;

		SequenceLayer_t* layers;
		int numLayers;

		int numIKLocks;
		IKLockData_t* ikLocks;

		const char* keyValues;

		const char* cyclePose;

		SequenceActMod_t* actmods;
		int numActMods;

		int ikResetMask;

		const uint32_t EstimateOptionCount_Seq() const
		{
			uint32_t numOptions = 1u; // name

			// add option per feature used, and options per animation data type if needed
			numOptions += options.features == AnimationOptions_t::ANIM_OPT_FEATURE_ALL ? EstimateOptionCount_Anim() : options.GetUsedOptions();

			// add option per sequence data type
			numOptions += (numEvents + numLayers + numIKLocks + numActMods);

			numOptions += HasActivity();
			numOptions += SEQ_ADD_OPT(weightlist);
			numOptions += GetHeight() + SEQ_ADD_OPT(numBlends > 1); // blends & blendwidth
			numOptions += SEQ_ADD_OPT(param[0]);
			numOptions += SEQ_ADD_OPT(param[1]);
			numOptions += SEQ_ADD_OPT(localentrynode && localexitnode);
			numOptions += SEQ_ADD_OPT(exitphase!= 0.0f);
			numOptions += SEQ_ADD_OPT(keyValues);

			numOptions += SEQ_ADD_OPT(fadeintime != s_Sequence_DefaultFadeIn);
			numOptions += SEQ_ADD_OPT(fadeouttime != s_Sequence_DefaultFadeOut);

			numOptions += SEQ_ADD_OPT(ikResetMask);

			return numOptions;
		}

		inline void SetupNodes(const char* const entryNode, const char* const exitNode, const int localEntryNode, const int localExitNode, const int nodeFlags)
		{
			entrynode = entryNode;
			exitnode = exitNode;
			localentrynode = localEntryNode;
			localexitnode = localExitNode;
			nodeflags = nodeFlags;
		}

		// event
		inline void SetEvent(const int index, const char* const event, const int id, const bool useNewStyle, const float cycle, const float unk, const char* const optionList)
		{
			assertm(events, "no valid events");
			assertm(index < numEvents && index >= 0, "invalid event index");

			events[index] = SequenceEvent_t(event, id, useNewStyle, cycle, unk, optionList);
		}

		inline void EnableEvents(QCFile* const file, const int eventCount)
		{
			numEvents = eventCount;
			events = reinterpret_cast<SequenceEvent_t*>(file->ReserveData(sizeof(SequenceEvent_t) * numEvents));
		}

		// blends
		inline void SetBlend(const int index, const char* const path)
		{
			assertm(blends, "no valid blends");
			assertm(index < numBlends && index >= 0, "invalid blend index");

			blends[index] = path;
		}

		inline void EnableBlends(QCFile* const file, const int blendCount)
		{
			numBlends = blendCount;

			assertm(groupsize[0] * groupsize[1] == numBlends, "invalid blend setup");

			blends = reinterpret_cast<const char**>(file->ReserveData(sizeof(char*) * numBlends));
		}

		// param
		inline void SetParam(const int index, const char* const paramName, const float paramStart, const float paramEnd)
		{
			assertm(index < 2 && index >= 0, "invalid layer index");

			param[index] = paramName;
			paramstart[index] = paramStart;
			paramend[index] = paramEnd;
		}

		// layers
		inline void SetLayer(const int index, const char* const layerSequence, const char* const layerPose, const int layerFlags, const float layerStart, const float layerPeak, const float layerTail, const float layerEnd)
		{
			assertm(layers, "no valid layers");
			assertm(index < numLayers && index >= 0, "invalid layer index");

			layers[index] = SequenceLayer_t(layerSequence, layerPose, layerFlags, layerStart, layerPeak, layerTail, layerEnd);
		}

		inline void EnableLayers(QCFile* const file, const int layerCount)
		{
			numLayers = layerCount;
			layers = reinterpret_cast<SequenceLayer_t*>(file->ReserveData(sizeof(SequenceLayer_t) * numLayers));
		}

		// iklocks
		inline void SetIKLock(const int index, const char* const chainName, const float posWeight, const float qWeight)
		{
			assertm(ikLocks, "no valid iklocks");
			assertm(index < numIKLocks && index >= 0, "invalid iklock index");

			ikLocks[index] = IKLockData_t(chainName, posWeight, qWeight);
		}

		inline void EnableIKLocks(QCFile* const file, const int ikLockCount)
		{
			numIKLocks = ikLockCount;
			ikLocks = reinterpret_cast<IKLockData_t*>(file->ReserveData(sizeof(IKLockData_t) * numIKLocks));
		}

		// cyclepose
		inline void SetCyclePose(const char* const poseparam)
		{
			options.SetCyclePose();
			cyclePose = poseparam;
		}

		// actmod
		inline void SetActMod(const int index, const char* const actName, const bool actNegated = false)
		{
			assertm(actmods, "no valid actmods");
			assertm(index < numActMods && index >= 0, "invalid actmod index");

			actmods[index] = SequenceActMod_t(actName, actNegated);
		}

		inline void EnableActMods(QCFile* const file, const int actModCount)
		{
			numActMods = actModCount;
			actmods = reinterpret_cast<SequenceActMod_t*>(file->ReserveData(sizeof(SequenceActMod_t) * numActMods));
		}
	};
#undef SEQ_ADD_OPT
	

	// custom commands	
	// misc
	size_t CommandKeyvalues_Write(const Command_t* const command, char* buffer, size_t bufferSize);
	size_t CommandCollText_Write(const Command_t* const command, char* buffer, size_t bufferSize);

	CommandOption_t* CommandConstDirectLight_ParseBinary(QCFile* const file, uint32_t* const numOptions, const void* const data, const uint32_t count, const bool store);
	CommandOption_t* CommandIllumPosition_ParseBinary(QCFile* const file, uint32_t* const numOptions, const void* const data, const uint32_t count, const bool store);
	CommandOption_t* CommandMaxEyeDeflect_ParseBinary(QCFile* const file, uint32_t* const numOptions, const void* const data, const uint32_t count, const bool store);

	// materials
	CommandOption_t* CommandTextureGroup_ParseBinary(QCFile* const file, uint32_t* const numOptions, const void* const data, const uint32_t count, const bool store);

	// bones
	CommandOption_t* CommandDefineBone_ParseBinary(QCFile* const file, uint32_t* const numOptions, const void* const data, const uint32_t count, const bool store);
	CommandOption_t* CommandJointContents_ParseBinary(QCFile* const file, uint32_t* const numOptions, const void* const data, const uint32_t count, const bool store);
	//CommandOption_t* CommandJointSurfaceProp_ParseBinary(QCFile* const file, uint32_t* const numOptions, const void* const data, const uint32_t count, const bool store);
	CommandOption_t* CommandScreenAlign_ParseBinary(QCFile* const file, uint32_t* const numOptions, const void* const data, const uint32_t count, const bool store);
	CommandOption_t* CommandBoneSaveFrame_ParseBinary(QCFile* const file, uint32_t* const numOptions, const void* const data, const uint32_t count, const bool store);
	CommandOption_t* CommandJiggleBone_ParseBinary(QCFile* const file, uint32_t* const numOptions, const void* const data, const uint32_t count, const bool store);
	CommandOption_t* CommandAttachment_ParseBinary(QCFile* const file, uint32_t* const numOptions, const void* const data, const uint32_t count, const bool store);

	// models
	CommandOption_t* CommandBodyGroup_ParseBinary(QCFile* const file, uint32_t* const numOptions, const void* const data, const uint32_t count, const bool store);
	CommandOption_t* CommandLOD_ParseBinary(QCFile* const file, uint32_t* const numOptions, const void* const data, const uint32_t count, const bool store);

	// animation
	CommandOption_t* CommandPoseParam_ParseBinary(QCFile* const file, uint32_t* const numOptions, const void* const data, const uint32_t count, const bool store);
	CommandOption_t* CommandIKChain_ParseBinary(QCFile* const file, uint32_t* const numOptions, const void* const data, const uint32_t count, const bool store);
	CommandOption_t* CommandIKAutoPlayLock_ParseBinary(QCFile* const file, uint32_t* const numOptions, const void* const data, const uint32_t count, const bool store);
	CommandOption_t* CommandSectionFrames_ParseBinary(QCFile* const file, uint32_t* const numOptions, const void* const data, const uint32_t count, const bool store);
	CommandOption_t* CommandAnimBlockSize_ParseBinary(QCFile* const file, uint32_t* const numOptions, const void* const data, const uint32_t count, const bool store);
	CommandOption_t* CommandWeightList_ParseBinary(QCFile* const file, uint32_t* const numOptions, const void* const data, const uint32_t count, const bool store);
	CommandOption_t* CommandAnimation_ParseBinary(QCFile* const file, uint32_t* const numOptions, const void* const data, const uint32_t count, const bool store);
	CommandOption_t* CommandSequence_ParseBinary(QCFile* const file, uint32_t* const numOptions, const void* const data, const uint32_t count, const bool store);

	// collision
	inline void CommandContents_ParseOptions(CommandOption_t* const options, const uint32_t usedOptions, uint32_t& optionsIndex, const int contents);
	CommandOption_t* CommandContents_ParseBinary(QCFile* const file, uint32_t* const numOptions, const void* const data, const uint32_t count, const bool store);
	CommandOption_t* CommandHBox_ParseBinary(QCFile* const file, uint32_t* const numOptions, const void* const data, const uint32_t count, const bool store);
	CommandOption_t* CommandCollModel_ParseBinary(QCFile* const file, uint32_t* const numOptions, const void* const data, const uint32_t count, const bool store);


	static const CommandInfo_t s_CommandList[CommandList_t::QC_COMMANDCOUNT] =
	{
		// misc
		CommandInfo_t(QC_MODELNAME,					"$modelname",					QCI_BASE,		&CommandGeneric_Write,	&CommandString_ParseBinary,			nullptr),
		CommandInfo_t(QC_STATICPROP,				"$staticprop",					QCI_BASE,		&CommandGeneric_Write,	&CommandNone_ParseBinary,			nullptr),
		CommandInfo_t(QC_OPAQUE,					"$opaque",						QCI_BASE,		&CommandGeneric_Write,	&CommandNone_ParseBinary,			nullptr),
		CommandInfo_t(QC_MOSTLYOPAQUE,				"$mostlyopaque",				QCI_BASE,		&CommandGeneric_Write,	&CommandNone_ParseBinary,			nullptr, s_QCVersion_MIN, s_QCVersion_CSGO),
		CommandInfo_t(QC_OBSOLETE,					"$obsolete",					QCI_BASE,		&CommandGeneric_Write,	&CommandNone_ParseBinary,			nullptr),
		CommandInfo_t(QC_NOFORCEDFADE,				"$noforcedfade",				QCI_BASE,		&CommandGeneric_Write,	&CommandNone_ParseBinary,			nullptr),
		CommandInfo_t(QC_FORCEPHONEMECROSSFADE,		"$forcephonemecrossfade",		QCI_BASE,		&CommandGeneric_Write,	&CommandNone_ParseBinary,			nullptr),
		CommandInfo_t(QC_CONSTANTDIRECTIONALLIGHT,	"$constantdirectionallight",	QCI_BASE,		&CommandGeneric_Write,	&CommandConstDirectLight_ParseBinary,	nullptr),
		CommandInfo_t(QC_AMBIENTBOOST,				"$ambientboost",				QCI_BASE,		&CommandGeneric_Write,	&CommandNone_ParseBinary,			nullptr),
		CommandInfo_t(QC_DONOTCASTSHADOWS,			"$donotcastshadows",			QCI_BASE,		&CommandGeneric_Write,	&CommandNone_ParseBinary,			nullptr),
		CommandInfo_t(QC_CASTTEXTURESHADOWS,		"$casttextureshadows",			QCI_BASE,		&CommandGeneric_Write,	&CommandNone_ParseBinary,			nullptr),
		CommandInfo_t(QC_SUBD,						"$subd",						QCI_BASE,		&CommandGeneric_Write,	&CommandNone_ParseBinary,			nullptr, s_QCVersion_MIN, s_QCVersion_R2),
		CommandInfo_t(QC_FADEDISTANCE,				"$fadedistance",				QCI_BASE,		&CommandGeneric_Write,	&CommandFloat_ParseBinary,			nullptr, s_QCVersion_R1, s_QCVersion_R5_RETAIL),
		CommandInfo_t(QC_ILLUMPOSITION,				"$illumposition",				QCI_BASE,		&CommandGeneric_Write,	&CommandIllumPosition_ParseBinary,	nullptr),
		CommandInfo_t(QC_EYEPOSITION,				"$eyeposition",					QCI_BASE,		&CommandGeneric_Write,	&CommandFloat_ParseBinary,			nullptr),
		CommandInfo_t(QC_MAXEYEDEFLECTION,			"$maxeyedeflection",			QCI_BASE,		&CommandGeneric_Write,	&CommandMaxEyeDeflect_ParseBinary,	nullptr, s_QCVersion_MIN, s_QCVersion_R1),
		CommandInfo_t(QC_AUTOCENTER,				"$autocenter",					QCI_BASE,		&CommandGeneric_Write,	&CommandNone_ParseBinary,			nullptr),
		
		CommandInfo_t(QC_KEYVALUES,					"$keyvalues",					QCI_BASE,		&CommandKeyvalues_Write,&CommandString_ParseBinary,			nullptr),

		CommandInfo_t(QC_INCLUDE,					"$include",						QCI_BASE,		&CommandGeneric_Write,	&CommandString_ParseBinary,			nullptr),

		// models
		CommandInfo_t(QC_MAXVERTS,					"$maxverts",					QCI_MODEL,		&CommandGeneric_Write,	&CommandInt_ParseBinary,			nullptr, s_QCVersion_P2, s_QCVersion_MAX), // alien swarm
		CommandInfo_t(QC_USEDETAILEDWEIGHTS,		"$usedetailedweights",			QCI_MODEL,		&CommandGeneric_Write,	&CommandNone_ParseBinary,			nullptr, s_QCVersion_R1, s_QCVersion_R5_RETAIL),
		CommandInfo_t(QC_USEVERTEXCOLOR,			"$usevertexcolor",				QCI_MODEL,		&CommandGeneric_Write,	&CommandNone_ParseBinary,			nullptr, s_QCVersion_R1, s_QCVersion_R5_RETAIL),
		CommandInfo_t(QC_USEEXTRATEXCOORD,			"$useextratexcoords",			QCI_MODEL,		&CommandGeneric_Write,	&CommandNone_ParseBinary,			nullptr, s_QCVersion_R1, s_QCVersion_R5_RETAIL),
		CommandInfo_t(QC_BODY,						"$body",						QCI_MODEL,		&CommandGeneric_Write,	&CommandPair_ParseBinary,			nullptr),
		CommandInfo_t(QC_BODYGROUP,					"$bodygroup",					QCI_MODEL,		&CommandGeneric_Write,	&CommandBodyGroup_ParseBinary,		nullptr),
		CommandInfo_t(QC_MODEL,						"$model",						QCI_MODEL,		&CommandGeneric_Write,	nullptr,							nullptr),

		CommandInfo_t(QC_LOD,						"$lod",							QCI_LOD,		&CommandGeneric_Write,	&CommandLOD_ParseBinary,			nullptr),
		CommandInfo_t(QC_SHADOWLOD,					"$shadowlod",					QCI_LOD,		&CommandGeneric_Write,	&CommandLOD_ParseBinary,			nullptr),
		CommandInfo_t(QC_ALLOWROOTLODS,				"$allowrootlods",				QCI_LOD,		&CommandGeneric_Write,	&CommandByte_ParseBinary,			nullptr),
		CommandInfo_t(QC_MINLOD,					"$minlod",						QCI_LOD,		&CommandGeneric_Write,	&CommandByte_ParseBinary,			nullptr),

		// materials
		CommandInfo_t(QC_CDMATERIALS,				"$cdmaterials",					QCI_TEXTURE,	&CommandGeneric_Write,	&CommandString_ParseBinary,			nullptr, s_QCVersion_MIN, s_QCVersion_R5_150), // cdtextures removed in rmdl 16
		CommandInfo_t(QC_TEXTUREGROUP,				"$texturegroup",				QCI_TEXTURE,	&CommandGeneric_Write,	&CommandTextureGroup_ParseBinary,	nullptr),
		CommandInfo_t(QC_RENAMEMATERIAL,			"$renamematerial",				QCI_TEXTURE,	&CommandGeneric_Write,	&CommandPair_ParseBinary,			nullptr),

		// bones
		CommandInfo_t(QC_DEFINEBONE,				"$definebone",					QCI_BONE,		&CommandGeneric_Write,	&CommandDefineBone_ParseBinary,		nullptr),
		CommandInfo_t(QC_JOINTCONTENTS,				"$jointcontents",				QCI_BONE,		&CommandGeneric_Write,	&CommandJointContents_ParseBinary,	nullptr),
		CommandInfo_t(QC_JOINTSURFACEPROP,			"$jointsurfaceprop",			QCI_BONE,		&CommandGeneric_Write,	&CommandPair_ParseBinary,			nullptr),
		CommandInfo_t(QC_BONEMERGE,					"$bonemerge",					QCI_BONE,		&CommandGeneric_Write,	&CommandString_ParseBinary,			nullptr),
		CommandInfo_t(QC_SCREENALIGN,				"$screenalign",					QCI_BONE,		&CommandGeneric_Write,	&CommandScreenAlign_ParseBinary,	nullptr),
		CommandInfo_t(QC_LIMITROTATION,				"$limitrotation",				QCI_BONE,		&CommandGeneric_Write,	&CommandString_ParseBinary,			nullptr),
		CommandInfo_t(QC_BONESAVEFRAME,				"$limitrotation",				QCI_BONE,		&CommandGeneric_Write,	&CommandBoneSaveFrame_ParseBinary,	nullptr),
		CommandInfo_t(QC_JIGGLEBONE,				"$jigglebone",					QCI_BONE,		&CommandGeneric_Write,	&CommandJiggleBone_ParseBinary,		nullptr, s_QCVersion_OB, s_QCVersion_MAX),
		CommandInfo_t(QC_ATTACHMENT,				"$attachment",					QCI_BONE,		&CommandGeneric_Write,	&CommandAttachment_ParseBinary,		nullptr),

		// animation
		CommandInfo_t(QC_POSEPARAMETER,				"$poseparameter",				QCI_ANIM,		&CommandGeneric_Write,	&CommandPoseParam_ParseBinary,		nullptr),
		CommandInfo_t(QC_IKCHAIN,					"$ikchain",						QCI_ANIM,		&CommandGeneric_Write,	&CommandIKChain_ParseBinary,		nullptr),
		CommandInfo_t(QC_IKAUTOPLAYLOCK,			"$ikautoplaylock",				QCI_ANIM,		&CommandGeneric_Write,	&CommandIKAutoPlayLock_ParseBinary,	nullptr, s_QCVersion_MIN, s_QCVersion_R5_150),
		CommandInfo_t(QC_INCLUDEMODEL,				"$includemodel",				QCI_ANIM,		&CommandGeneric_Write,	&CommandString_ParseBinary,			nullptr),
		CommandInfo_t(QC_SECTIONFRAMES,				"$sectionframes",				QCI_ANIM,		&CommandGeneric_Write,	&CommandSectionFrames_ParseBinary,	nullptr),
		CommandInfo_t(QC_ANIMBLOCKSIZE,				"$animblocksize",				QCI_ANIM,		&CommandGeneric_Write,	&CommandAnimBlockSize_ParseBinary,	nullptr, s_QCVersion_MIN, s_QCVersion_R1),
		CommandInfo_t(QC_DEFAULTWEIGHTLIST,			"$defaultweightlist",			QCI_ANIM,		&CommandGeneric_Write,	&CommandWeightList_ParseBinary,		nullptr),
		CommandInfo_t(QC_WEIGHTLIST,				"$weightlist",					QCI_ANIM,		&CommandGeneric_Write,	&CommandWeightList_ParseBinary,		nullptr),
		CommandInfo_t(QC_ANIMATION,					"$animation",					QCI_ANIM,		&CommandGeneric_Write,	&CommandAnimation_ParseBinary,		nullptr),
		CommandInfo_t(QC_DECLAREANIM,				"$declareanimation",			QCI_ANIM,		&CommandGeneric_Write,	&CommandString_ParseBinary,			nullptr),
		CommandInfo_t(QC_SEQUENCE,					"$sequence",					QCI_ANIM,		&CommandGeneric_Write,	&CommandSequence_ParseBinary,		nullptr),
		CommandInfo_t(QC_DECLARESEQ,				"$declaresequence",				QCI_ANIM,		&CommandGeneric_Write,	&CommandString_ParseBinary,			nullptr),

		// box
		CommandInfo_t(QC_BBOX,						"$bbox",						QCI_BOX,		&CommandGeneric_Write,	&CommandPair_ParseBinary,			nullptr),
		CommandInfo_t(QC_CBOX,						"$cbox",						QCI_BOX,		&CommandGeneric_Write,	&CommandPair_ParseBinary,			nullptr),
		CommandInfo_t(QC_HBOXSET,					"$hboxset",						QCI_BOX,		&CommandGeneric_Write,	&CommandString_ParseBinary,			nullptr),
		CommandInfo_t(QC_HBOX,						"$hbox",						QCI_BOX,		&CommandGeneric_Write,	&CommandHBox_ParseBinary,			nullptr),
		CommandInfo_t(QC_HGROUP,					"$hgroup",						QCI_BOX,		&CommandGeneric_Write,	&CommandPair_ParseBinary,			nullptr),
		CommandInfo_t(QC_SKIPBONEINBBOX,			"$skipboneinbbox",				QCI_BOX,		&CommandGeneric_Write,	&CommandNone_ParseBinary,			nullptr),

		// collision
		CommandInfo_t(QC_CONTENTS,					"$contents",					QCI_COLLISON,	&CommandGeneric_Write,	&CommandContents_ParseBinary,		nullptr),
		CommandInfo_t(QC_SURFACEPROP,				"$surfaceprop",					QCI_COLLISON,	&CommandGeneric_Write,	&CommandString_ParseBinary,			nullptr),
		CommandInfo_t(QC_COLLISIONMODEL,			"$collisionmodel",				QCI_COLLISON,	&CommandGeneric_Write,	&CommandCollModel_ParseBinary,		nullptr),
		CommandInfo_t(QC_COLLISIONJOINTS,			"$collisionjoints",				QCI_COLLISON,	&CommandGeneric_Write,	&CommandCollModel_ParseBinary,		nullptr),
		CommandInfo_t(QC_COLLISIONTEXT,				"$collisiontext",				QCI_COLLISON,	&CommandCollText_Write,	&CommandString_ParseBinary,			nullptr),

	};

	inline const CommandInfo_t* const CmdInfo(const CommandList_t cmd) { return &s_CommandList[cmd]; }


	// verify order of CommandInfo_t
	inline const bool VerifyOrder()
	{
#ifdef _DEBUG
		for (short i = 0; i < QC_COMMANDCOUNT; i++)
		{
			const CommandList_t cmdid = static_cast<CommandList_t>(i);

			if (s_CommandList[i].id == cmdid)
				continue;

			printf("command at %i was %i, expected %i...\n", i, s_CommandList[i].id, cmdid);
			assertm(false, "command out of order");
			return false;
		}
#endif

		return true;
	}

	typedef uint16_t CommandIndex_t;
	constexpr CommandIndex_t s_MaxCommandIndex = 0xFFFF;

	struct Command_t
	{
		Command_t() : info(nullptr), options(nullptr), numOptions(0), format(0u), index(s_MaxCommandIndex) {}
		Command_t(const CommandInfo_t* cmdinfo) : info(cmdinfo), options(nullptr), numOptions(0), format(0u), index(s_MaxCommandIndex) {}
		Command_t(Command_t& cmd) : info(cmd.info), options(cmd.options), numOptions(cmd.numOptions), format(cmd.format), index(cmd.index)
		{
			cmd.options = nullptr;
			cmd.numOptions = 0;
		}

		Command_t(Command_t&& cmd) noexcept : info(cmd.info), options(cmd.options), numOptions(cmd.numOptions), format(cmd.format), index(cmd.index)
		{
			cmd.options = nullptr;
			cmd.numOptions = 0;
		}

		~Command_t()
		{
			FreeAllocArray(options);
		}

		const CommandInfo_t* info;

		CommandOption_t* options;
		uint32_t numOptions;

		// formatting options
		CommandFormat_t format;
		CommandIndex_t index; // local index within this qc file

		void ParseBinaryOptions(QCFile* const file, const void* const data, const int count = 1, const bool store = false)
		{
			assertm(info->parseBinaryFunc, "binary parse func was nullptr");
			options = info->parseBinaryFunc(file, &numOptions, data, count, store);
		}

		inline const CommandType_t GetType() const { return info->type; }
		inline const CommandList_t GetCmd() const { return info->id; }
		inline const CommandIndex_t GetIndex() const { return index; }

		inline const bool ArrayBracket() const { return format & QC_FMT_ARRAY; }
		inline const bool IsComment() const { return format & QC_FMT_COMMENT; }
		inline const bool WriteName() const { return format & QC_FMT_WRITENAME; }
		inline const bool ParentLine() const { return format & QC_FMT_PARENTLINE; }
		inline const bool NewLine() const { return format & QC_FMT_NEWLINE; }
		inline const bool BracketStyle() const { return format & QC_FMT_ARRAYSTYLE; }
	};

	inline void CmdParse(QCFile* const file, const CommandList_t cmd, const void* const data, const int count = 1, const bool store = false, const CommandFormat_t fmt = QC_FMT_NONE)
	{
		Command_t command(CmdInfo(cmd));
		command.ParseBinaryOptions(file, data, count, store);
		command.format = fmt;

		// set our index to retain add order during sorting
		command.index = static_cast<uint16_t>(file->NumCommands());
		assertm(command.index < s_MaxCommandIndex, "exceeded max commands");

		file->AddCommand(command);
	}
}