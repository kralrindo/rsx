#include <pch.h>

#include <core/mdl/qc.h>

extern inline void StaticPropFlipFlop(Vector& in);

namespace qc
{
	static Version_t g_QCExportVersion = s_QCVersion_P2; // global version for exporting qc, only downside is you can't set per model, but who was gonna anyway?

	//
	// QCFile
	//

	bool QCFile::CommandToText(const Command_t* const cmd)
	{
		// cmd buff
		CommandWriteFunc_t const func = cmd->info->writeFunc;
		assertm(func, "writer func was invalid");
		if (!func)
		{
			printf("a %s command was skipped because it didn't have a write function", cmd->info->name);
			return false;
		}

		const size_t remaining = func(cmd, cmdbuf, maxCommandLength);
		assertm(maxCommandLength >= remaining, "ran out of space");

		buffer.WriteString(cmdbuf);

		return true;
	}

	bool QCFile::ParseToText(const bool useIncludeFiles)
	{
		// sort commands
		// grouped by command in groups of the same type
		const size_t numCommands = NumCommands();

		const Command_t** const sorted = reinterpret_cast<const Command_t** const>(buffer.Writer());
		const size_t sortedSize = sizeof(intptr_t) * (numCommands + (CommandType_t::QCI_COUNT * useIncludeFiles));
		buffer.AdvanceWriter(sortedSize);

		const Command_t** temp = reinterpret_cast<const Command_t** const>(buffer.Writer());
		 assertm(buffer.Capacity() >= (sizeof(intptr_t) * numCommands * 2), "out of space!");

		size_t numSortedTotal = 0ull;
		for (uint16_t i = 0; i < CommandType_t::QCI_COUNT; i++)
		{
			const uint32_t numSorted = s_CommandTypeSortFunc[i](commands.data(), temp, numCommands, static_cast<CommandType_t>(i));

			if (!numSorted)
				continue;

			memcpy_s(sorted + numSortedTotal, sortedSize - (numSortedTotal * sizeof(intptr_t)), temp, numSorted * sizeof(intptr_t));
			numSortedTotal += numSorted;
		}

		// save base position
		buffer.SetTextStart();

		// write qci files if used
		if (useIncludeFiles)
		{
			std::filesystem::path qcifile(path);
			const std::string stem = path.stem().string();

			for (uint16_t i = 0; i < CommandType_t::QCI_COUNT; i++)
			{
				if (!s_CommandTypeNames[i])
					continue;

				bool typeUsed = false;
				CommandList_t prevCmd = QC_CMDBASE;
				for (size_t cmdIdx = 0; cmdIdx < numCommands; cmdIdx++)
				{
					const Command_t* const cmd = sorted[cmdIdx];

					// check if this command is supported for the desired version
					if (!cmd->info->VersionSupported(g_QCExportVersion))
						continue;

					const CommandType_t type = cmd->GetType();

					// not the correct type
					if (type != i)
						continue;

					typeUsed = true;

					// add new line if it's a different command and in the same group
					if (prevCmd != cmd->GetCmd() && s_CommandList[prevCmd].type == type)
					{
						buffer.WriteCharacter('\n');
					}

					CommandToText(cmd);

					prevCmd = cmd->GetCmd();
				}

				// skip if unused
				if (!typeUsed)
					continue;

				// save this because we do some funky shit
				const size_t fileLength = buffer.TextLength();

				const char* const filename = buffer.Writer();
				buffer.WriteFormatted("%s%s%s\0", stem.c_str(), s_CommandTypeNames[i], s_QCFileExtensions[QCFileExt::QC_EXT_QCI]);
				qcifile.replace_filename(filename);

				// write the qci file
#ifndef STREAMIO
				std::ofstream file(qcifile);
				file.write(buffer.Text(), fileLength);
#else
				StreamIO file(qcifile, eStreamIOMode::Write);
				file.write(buffer.Text(), fileLength);
#endif // !STREAMIO

				buffer.WriterToText();

				// include command
				// todo the placement of these can have huge implications on compile
				CmdParse(this, QC_INCLUDE, filename, 1, true);
				sorted[NumCommands() - 1] = &commands.back();

				buffer.SetTextStart();
			}
		}

		CommandList_t prevCmd = QC_CMDBASE;
		for (size_t cmdIdx = 0; cmdIdx < NumCommands(); cmdIdx++)
		{
			const Command_t* const cmd = sorted[cmdIdx];

			// check if this command is supported for the desired version
			if (!cmd->info->VersionSupported(g_QCExportVersion))
				continue;

			// if we are using qci files, skip commands that are written in qci files
			if (useIncludeFiles && s_CommandTypeNames[cmd->GetType()])
				continue;

			// commands should be in blocks with how we've sorted
			if (prevCmd != cmd->GetCmd())
			{
				buffer.WriteCharacter('\n');
			}

			CommandToText(cmd);

			prevCmd = cmd->GetCmd();
		}

#ifndef STREAMIO
		std::ofstream file(path);
		file.write(buffer.Text(), buffer.TextLength());
#else
		StreamIO file(path, eStreamIOMode::Write);
		file.write(buffer.Text(), buffer.TextLength());
#endif // !STREAMIO

		return true;
	}

	void QCFile::SetExportVersion(const Version_t version)
	{
		g_QCExportVersion = version;
	}


	//
	// GENERIC COMMANDS
	//

	// parse option formatting and call write function for option data
	void CommandOption_Write(const CommandOption_t* option, CTextBuffer* const text, const char* const whitespace, const CommentStyle_t commentStyle)
	{
		assertm((option->data.ParentLine() && option->data.NewLine()) == false, "'QC_FMT_PARENTLINE' and 'QC_FMT_NEWLINE' are not meant to be used together");

		// add a whitespace
		text->WriteString(whitespace);

		// file comments
		if (option->data.IsComment() && commentStyle == QC_COMMENT_ONELINE)
		{
			PTEXTBUFFER_WRITE_STRING_CT(text, "//");
		}
		else if (option->data.IsComment() && commentStyle == QC_COMMENT_MULTILINE)
		{
			PTEXTBUFFER_WRITE_STRING_CT(text, "/*");
		}

		// write the name, before curly brackets
		if (option->data.WriteName())
		{
			text->WriteFormatted("%s ", option->desc->name);
		}

		// array contained within curly brackets
		if (option->data.ArrayBracket())
		{
			PTEXTBUFFER_WRITE_STRING_CT(text, "{ ");
		}

		// write the data
		CommandOptionDataFunc_t const func = s_CommandOptionDataFunc[option->TypeAsChar()];
		assertm(func, "write func was nullptr");
		func(&option->data, text);

		// array contained within curly brackets
		if (option->data.ArrayBracket())
		{
			PTEXTBUFFER_WRITE_STRING_CT(text, " }");
		}

		// file comments
		if (option->data.IsComment() && commentStyle == QC_COMMENT_MULTILINE)
		{
			PTEXTBUFFER_WRITE_STRING_CT(text, "*/");
		}

		// add a new line
		if (option->data.NewLine())
		{
			text->WriteCharacter('\n');
		}
	}

	inline const CommandOption_t* const CommandGeneric_LastWritten(const CommandOption_t* options, const uint32_t currentIndex)
	{
		if (currentIndex == 0u)
			return nullptr;

		const CommandOption_t* const option = options + (currentIndex - 1);

		if (option->desc->VersionSupported(g_QCExportVersion))
			return option;

		return CommandGeneric_LastWritten(options, currentIndex - 1);
	}

	size_t CommandGeneric_Write(const Command_t* const command, char* buffer, size_t bufferSize)
	{
		const CommandInfo_t* const info = command->info;
		const CommandOption_t* const options = command->options;
		const uint32_t numOptions = command->numOptions;

		const bool formatComment = command->IsComment(); // are we using comment formatting on this command?

		CTextBuffer text(buffer, bufferSize);
		text.SetTextStart();

		// comment out this comand?
		if (formatComment)
		{
			TEXTBUFFER_WRITE_STRING_CT(text, "//");
		}

		text.WriteString(info->name);

		bool oneLine = true;
		for (uint32_t i = 0; i < numOptions; i++)
		{
			const CommandOption_t* const option = options + i;

			// check if this option is supported on desired qc version
			if (!option->desc->VersionSupported(g_QCExportVersion))
				continue;

			if (!option->data.ParentLine())
			{
				oneLine = false;
				continue;
			}

			CommandOption_Write(option, &text, " ", formatComment ? QC_COMMENT_NONE : QC_COMMENT_MULTILINE);
		}

		// add a new line
		text.WriteCharacter('\n');

		if (oneLine)
		{
			text.WriterToText();
			return text.Capacity();
		}

		// comment out this comand?
		if (formatComment)
		{
			TEXTBUFFER_WRITE_STRING_CT(text, "/*");
		}

		// add a bracket entry
		TEXTBUFFER_WRITE_STRING_CT(text, "{\n");

		for (uint32_t i = 0; i < numOptions; i++)
		{
			const CommandOption_t* const option = options + i;

			// check if this option is supported on desired qc version
			if (!option->desc->VersionSupported(g_QCExportVersion))
				continue;

			if (option->data.ParentLine())
				continue;

			// here we're checking if the previous command was written on the same line, don't use an indent if that's the case
			const char* whitespace = "\t";
			const CommandOption_t* const prevOption = CommandGeneric_LastWritten(options, i);

			// the previous command was written, on the same line (not the parent line), and did not create a new line
			if (prevOption && !prevOption->data.ParentLine() && !prevOption->data.NewLine())
			{
				whitespace = " ";
			}

			CommandOption_Write(option, &text, whitespace, formatComment ? QC_COMMENT_NONE : QC_COMMENT_ONELINE);
		}

		// add a bracket exit
		TEXTBUFFER_WRITE_STRING_CT(text, "}\n");

		// comment out this comand?
		if (formatComment)
		{
			TEXTBUFFER_WRITE_STRING_CT(text, "*/");
		}

		text.WriterToText();

		return text.Capacity();
	}


	//
	// OPTION DATA
	//

	inline void CommandOption_WriteNone(const CommandOptionData_t* const data, CTextBuffer* const text)
	{
		UNUSED(data);
		UNUSED(text);

		assertm(!data->count, "should contain no options");
	}

	inline void CommandOption_WriteString(const CommandOptionData_t* const data, CTextBuffer* const text)
	{
		const char* const*strings = nullptr;

		if (data->count > 1u)
		{
			strings = reinterpret_cast<const char* const*>(data->value.ptr);
		}
		else
		{
			strings = reinterpret_cast<const char* const*>(&data->value.ptr);
		}

		for (uint32_t i = 0u; i < data->count; i++)
		{
			// write whitespace if this isn't the last string
			if (i > 0u)
			{
				text->WriteCharacter(' ');
			}

			// [rika]: wrap in quotation marks
			text->WriteCharacter('\"');

			text->WriteString(strings[i]);

			// [rika]: wrap in quotation marks
			text->WriteCharacter('\"');
		}
	}

	inline void CommandOption_WriteFloat(const CommandOptionData_t* const data, CTextBuffer* const text)
	{
		const float* floats = nullptr;

		if (data->count > 2u)
		{
			floats = reinterpret_cast<const float* const>(data->value.ptr);
		}
		else
		{
			floats = reinterpret_cast<const float* const>(&data->value.raw);
		}

		for (uint32_t i = 0u; i < data->count; i++)
		{
			// write whitespace if this isn't the last string
			if (i > 0u)
			{
				text->WriteCharacter(' ');
			}

			text->WriteFormatted("%f", floats[i]);
		}
	}

	inline void CommandOption_WriteChar(const CommandOptionData_t* const data, CTextBuffer* const text)
	{
		const char* bytes = nullptr;

		if (data->count > 8u)
		{
			bytes = reinterpret_cast<const char* const>(data->value.ptr);
		}
		else
		{
			bytes = reinterpret_cast<const char* const>(&data->value.raw);
		}

		for (uint32_t i = 0u; i < data->count; i++)
		{
			// write whitespace if this isn't the last string
			if (i > 0u)
			{
				text->WriteCharacter(' ');
			}

			text->WriteFormatted("%i", bytes[i]);
		}
	}

	inline void CommandOption_WriteByte(const CommandOptionData_t* const data, CTextBuffer* const text)
	{
		const uint8_t* bytes = nullptr;

		if (data->count > 8u)
		{
			bytes = reinterpret_cast<const uint8_t* const>(data->value.ptr);
		}
		else
		{
			bytes = reinterpret_cast<const uint8_t* const>(&data->value.raw);
		}

		for (uint32_t i = 0u; i < data->count; i++)
		{
			// write whitespace if this isn't the last string
			if (i > 0u)
			{
				text->WriteCharacter(' ');
			}

			text->WriteFormatted("%u", bytes[i]);
		}
	}

	inline void CommandOption_WriteInt(const CommandOptionData_t* const data, CTextBuffer* const text)
	{
		const int* integers = nullptr;

		if (data->count > 2u)
		{
			integers = reinterpret_cast<const int* const>(data->value.ptr);
		}
		else
		{
			integers = reinterpret_cast<const int* const>(&data->value.raw);
		}

		for (uint32_t i = 0u; i < data->count; i++)
		{
			// write whitespace if this isn't the last string
			if (i > 0u)
			{
				text->WriteCharacter(' ');
			}

			text->WriteFormatted("%i", integers[i]);
		}
	}

	inline void CommandOption_WriteUInt(const CommandOptionData_t* const data, CTextBuffer* const text)
	{
		const uint32_t* integers = nullptr;

		if (data->count > 2u)
		{
			integers = reinterpret_cast<const uint32_t* const>(data->value.ptr);
		}
		else
		{
			integers = reinterpret_cast<const uint32_t* const>(&data->value.raw);
		}

		for (uint32_t i = 0u; i < data->count; i++)
		{
			// write whitespace if this isn't the last string
			if (i > 0u)
			{
				text->WriteCharacter(' ');
			}

			text->WriteFormatted("%u", integers[i]);
		}
	}

	// todo add other types
	inline void CommandOption_WritePair(const CommandOptionData_t* const data, CTextBuffer* const text)
	{
		const CommandOptionPair_t* const pair = reinterpret_cast<const CommandOptionPair_t* const>(data->value.ptr);

		for (uint32_t i = 0u; i < 2u; i++)
		{
			s_CommandOptionDataFunc[pair->type[i]](&pair->data[i], text);

			if (i == 0)
			{
				text->WriteCharacter(' ');
			}
		}
	}

	// todo add other types
	inline void CommandOption_WriteArray(const CommandOptionData_t* const data, CTextBuffer* const text)
	{
		const CommandOptionArray_t* const array = reinterpret_cast<const CommandOptionArray_t* const>(data->value.ptr);

		const bool formatComment = false; // todo: formatting

		uint32_t numInBracket = 0u;
		for (uint32_t i = 0u; i < array->numOptions; i++)
		{
			const CommandOption_t* const option = array->options + i;

			// check if this option is supported on desired qc version
			if (!option->desc->VersionSupported(g_QCExportVersion))
				continue;

			if (!option->data.ParentLine())
			{
				numInBracket++;
				continue;
			}

			CommandOption_Write(option, text, " ", formatComment ? QC_COMMENT_NONE : QC_COMMENT_MULTILINE);
		}

		text->WriteCharacter('\n');

		if (numInBracket == 0u)
		{
			return;
		}

		constexpr uint16_t bufferSize = s_QCMaxIndentation * 2;
		char indentation[bufferSize]{};
		assertm(s_QCMaxIndentation >= array->numIntendation, "array was nested to deeply");
		if (array->numIntendation >= s_QCMaxIndentation)
			return;

		for (uint16_t i = 0; i < array->numIntendation; i++)
		{
			indentation[i] = '\t';
		}

		// add a bracket entry
		text->WriteString(indentation);

		// comment out this comand?
		if (formatComment)
		{
			PTEXTBUFFER_WRITE_STRING_CT(text, "/*");
		}

		PTEXTBUFFER_WRITE_STRING_CT(text, "{\n");

		indentation[array->numIntendation] = '\t'; // indent once more!
		for (uint32_t i = 0u, idx = 0u; idx < numInBracket; i++)
		{
			if (i >= array->numOptions)
				break;

			const CommandOption_t* const option = array->options + i;

			// check if this option is supported on desired qc version
			if (!option->desc->VersionSupported(g_QCExportVersion))
				continue;

			if (option->data.ParentLine())
				continue;

			// here we're checking if the previous command was written on the same line, don't use an indent if that's the case
			const CommandOption_t* const prevOption = CommandGeneric_LastWritten(array->options, i);

			// the previous command was written, on the same line (not the parent line), and did not create a new line
			if (prevOption && !prevOption->data.ParentLine() && !prevOption->data.NewLine())
			{
				indentation[array->numIntendation] = ' '; // use a space
				CommandOption_Write(option, text, indentation, formatComment ? QC_COMMENT_NONE : QC_COMMENT_ONELINE);
				indentation[array->numIntendation] = '\t'; // replace with a tab again

				idx++;
				continue;
			}

			CommandOption_Write(option, text, indentation, formatComment ? QC_COMMENT_NONE : QC_COMMENT_ONELINE);

			idx++;
		}
		indentation[array->numIntendation] = '\0'; // we're outside, remove it

		// add a bracket exit
		text->WriteString(indentation);
		PTEXTBUFFER_WRITE_STRING_CT(text, "}\n");

		// comment out this comand?
		if (formatComment)
		{
			text->WriteString(indentation);
			PTEXTBUFFER_WRITE_STRING_CT(text, "*/");
		}

	}

	constexpr CommandOptionDesc_t s_CommandGeneric_Option_None(QC_OPT_NONE, "value");
	CommandOption_t* CommandNone_ParseBinary(QCFile* const file, uint32_t* const numOptions, const void* const data, const uint32_t count, const bool store = false)
	{
		UNUSED(file);
		UNUSED(data);
		UNUSED(count);
		UNUSED(store);

		*numOptions = 0;

		return nullptr;
	}

	constexpr CommandOptionDesc_t s_CommandGeneric_Option_String(QC_OPT_STRING, "value");
	CommandOption_t* CommandString_ParseBinary(QCFile* const file, uint32_t* const numOptions, const void* const data, const uint32_t count, const bool store = false)
	{
		UNUSED(count);

		CommandOption_t* const options = new CommandOption_t[1];
		options->Init(&s_CommandGeneric_Option_String);

		if (store)
		{
			options->SaveStr(file, reinterpret_cast<const char* const>(data));
		}
		else
		{
			options->SetPtr(data);
		}

		*numOptions = 1;

		return options;
	}

	constexpr CommandOptionDesc_t s_CommandGeneric_Option_Float(QC_OPT_FLOAT, "value");
	CommandOption_t* CommandFloat_ParseBinary(QCFile* const file, uint32_t* const numOptions, const void* const data, const uint32_t count, const bool store = false)
	{
		CommandOption_t* const options = new CommandOption_t[1];
		options->Init(&s_CommandGeneric_Option_Float);

		if (count > 2) {
			if (store)
				options->SavePtr(file, data, count, sizeof(float) * count);
			else
				options->SetPtr(data, count);
		}
		else
		{
			options->SetRaw(data, count, sizeof(float) * count);
		}

		*numOptions = 1;

		return options;
	}

	constexpr CommandOptionDesc_t s_CommandGeneric_Option_Byte(QC_OPT_BYTE, "value");
	CommandOption_t* CommandByte_ParseBinary(QCFile* const file, uint32_t* const numOptions, const void* const data, const uint32_t count, const bool store = false)
	{
		CommandOption_t* const options = new CommandOption_t[1];
		options->Init(&s_CommandGeneric_Option_Byte);

		if (count > 8)
		{
			if (store)
				options->SavePtr(file, data, count, count);
			else
				options->SetPtr(data, count);
		}
		else
		{
			options->SetRaw(data, count, count);
		}

		*numOptions = 1;

		return options;
	}

	constexpr CommandOptionDesc_t s_CommandGeneric_Option_Int(QC_OPT_INT, "value");
	CommandOption_t* CommandInt_ParseBinary(QCFile* const file, uint32_t* const numOptions, const void* const data, const uint32_t count, const bool store = false)
	{
		CommandOption_t* const options = new CommandOption_t[1];
		options->Init(&s_CommandGeneric_Option_Int);

		if (count > 2)
		{
			if (store)
				options->SavePtr(file, data, count, sizeof(int) * count);
			else
				options->SetPtr(data, count);
		}
		else
		{
			options->SetRaw(data, count, sizeof(int) * count);
		}

		*numOptions = 1;

		return options;
	}

	constexpr CommandOptionDesc_t s_CommandGeneric_Option_Pair(QC_OPT_PAIR, "value");
	CommandOption_t* CommandPair_ParseBinary(QCFile* const file, uint32_t* const numOptions, const void* const data, const uint32_t count, const bool store = false)
	{
		assertm(count == 1, "array of pairs not supported in this func");

		const CommandOptionPair_t* pair = reinterpret_cast<const CommandOptionPair_t* const>(data);

		CommandOption_t* const options = new CommandOption_t[1];

		options->Init(&s_CommandGeneric_Option_Pair);

		if (store)
		{
			CommandOptionPair_t tmp;
			memcpy_s(&tmp, sizeof(CommandOptionPair_t), pair, sizeof(CommandOptionPair_t));

			for (uint32_t idx = 0; idx < 2; idx++)
			{
				CommandOptionData_t* const pairData = &tmp.data[idx];

				switch (pair->type[idx])
				{
				case CommandOptionType_t::QC_OPT_STRING:
				{
					// todo: fix this!
					assertm(pairData->count == 1, "no array string ty");
					pairData->SaveStr(file, reinterpret_cast<const char*>(pairData->value.ptr));

					break;
				}
				case CommandOptionType_t::QC_OPT_FLOAT:
				case CommandOptionType_t::QC_OPT_INT:
				{
					if (pairData->count <= 2)
						break;

					pairData->SavePtr(file, pairData->value.ptr, pairData->count, 4u * pairData->count);

					break;
				}
				case CommandOptionType_t::QC_OPT_BYTE:
				{
					if (pairData->count <= 8)
						break;

					pairData->SavePtr(file, pairData->value.ptr, pairData->count, pairData->count);

					break;
				}
				case CommandOptionType_t::QC_OPT_NONE:
				case CommandOptionType_t::QC_OPT_PAIR:
				default:
				{
					break;
				}
				}
			}

			options->SavePtr(file, &tmp, count, sizeof(CommandOptionPair_t));
		}
		else
		{

			options->SavePtr(file, data, count, sizeof(CommandOptionPair_t));
		}

		*numOptions = 1;

		return options;
	}


	//
	// CUSTOM COMMANDS
	//

	//
	// static options
	//
	
	// misc
	constexpr CommandOptionDesc_t s_CommandGeneric_Option_Name(QC_OPT_STRING, "name");
	constexpr CommandOptionDesc_t s_CommandGeneric_Option_Pos(QC_OPT_FLOAT, "pos");
	constexpr CommandOptionDesc_t s_CommandGeneric_Option_Rot(QC_OPT_FLOAT, "rot");
	constexpr CommandOptionDesc_t s_CommandGeneric_Option_Bone(QC_OPT_STRING, "bone");

	// materials
	constexpr CommandOptionDesc_t s_CommandTextureGroup_Option_Name(QC_OPT_STRING, "skin_name", QC_FMT_NONE, s_QCVersion_R5_080, s_QCVersion_R5_RETAIL);
	constexpr CommandOptionDesc_t s_CommandTextureGroup_Option_Skin(QC_OPT_STRING, "skin", (QC_FMT_ARRAY | QC_FMT_NEWLINE));

	// models

	// bones
	constexpr CommandOptionDesc_t s_CommandDefineBone_Option_Parent(QC_OPT_STRING, "parent");
	//static const CommandOptionDesc_t s_CommandDefineBone_Option_Pos(QC_OPT_FLOAT, "pos");
	//static const CommandOptionDesc_t s_CommandDefineBone_Option_Rot(QC_OPT_FLOAT, "rot");
	constexpr CommandOptionDesc_t s_CommandDefineBone_Option_FixupPos(QC_OPT_FLOAT, "fixupPos");
	constexpr CommandOptionDesc_t s_CommandDefineBone_Option_FixupRot(QC_OPT_FLOAT, "fixupRot");
	constexpr const char* const s_CommandDefineBone_Value_Parent = "";
	constexpr CommandOptionDesc_t s_CommandAttachment_Option_Type(QC_OPT_STRING, "type");

	// jiggly wiggly boing boing
	constexpr CommandOptionDesc_t s_CommandJiggleBone_Option_IsRigid(QC_OPT_ARRAY, "is_rigid", s_CommandOptionFormatNameNewLine);
	constexpr CommandOptionDesc_t s_CommandJiggleBone_Option_IsFlexible(QC_OPT_ARRAY, "is_flexible", s_CommandOptionFormatNameNewLine);
	constexpr CommandOptionDesc_t s_CommandJiggleBone_Option_BaseSpring(QC_OPT_ARRAY, "has_base_spring", s_CommandOptionFormatNameNewLine);
	constexpr CommandOptionDesc_t s_CommandJiggleBone_Option_IsBoing(QC_OPT_ARRAY, "is_boing", s_CommandOptionFormatNameNewLine);

	// common
	constexpr CommandOptionDesc_t s_CommandJiggleBone_Option_Length(QC_OPT_FLOAT, "length", s_CommandOptionFormatNameNewLine);
	constexpr CommandOptionDesc_t s_CommandJiggleBone_Option_TipMass(QC_OPT_FLOAT, "tip_mass", s_CommandOptionFormatNameNewLine);
	constexpr CommandOptionDesc_t s_CommandJiggleBone_Option_TipFriction(QC_OPT_FLOAT, "tip_friction", s_CommandOptionFormatNameNewLine, s_QCVersion_R5_080, s_QCVersion_R5_RETAIL);
	constexpr CommandOptionDesc_t s_CommandJiggleBone_Option_NoFlex(QC_OPT_NONE, "no_flex", s_CommandOptionFormatNameNewLine, s_QCVersion_R5_080, s_QCVersion_R5_RETAIL);

	// constraints
	constexpr CommandOptionDesc_t s_CommandJiggleBone_Option_AngleConstraint(QC_OPT_FLOAT, "angle_constraint", s_CommandOptionFormatNameNewLine);

	constexpr CommandOptionDesc_t s_CommandJiggleBone_Option_YawConstraint(QC_OPT_FLOAT, "yaw_constraint", s_CommandOptionFormatNameNewLine);
	constexpr CommandOptionDesc_t s_CommandJiggleBone_Option_YawFriction(QC_OPT_FLOAT, "yaw_friction", s_CommandOptionFormatNameNewLine);
	constexpr CommandOptionDesc_t s_CommandJiggleBone_Option_YawBounce(QC_OPT_FLOAT, "yaw_bounce", s_CommandOptionFormatNameNewLine);

	constexpr CommandOptionDesc_t s_CommandJiggleBone_Option_PitchConstraint(QC_OPT_FLOAT, "pitch_constraint", s_CommandOptionFormatNameNewLine);
	constexpr CommandOptionDesc_t s_CommandJiggleBone_Option_PitchFriction(QC_OPT_FLOAT, "pitch_friction", s_CommandOptionFormatNameNewLine);
	constexpr CommandOptionDesc_t s_CommandJiggleBone_Option_PitchBounce(QC_OPT_FLOAT, "pitch_bounce", s_CommandOptionFormatNameNewLine);

	// flexible
	constexpr CommandOptionDesc_t s_CommandJiggleBone_Option_YawStiffness(QC_OPT_FLOAT, "yaw_stiffness", s_CommandOptionFormatNameNewLine);
	constexpr CommandOptionDesc_t s_CommandJiggleBone_Option_YawDamping(QC_OPT_FLOAT, "yaw_damping", s_CommandOptionFormatNameNewLine);
	constexpr CommandOptionDesc_t s_CommandJiggleBone_Option_PitchStiffness(QC_OPT_FLOAT, "pitch_stiffness", s_CommandOptionFormatNameNewLine);
	constexpr CommandOptionDesc_t s_CommandJiggleBone_Option_PitchDamping(QC_OPT_FLOAT, "pitch_damping", s_CommandOptionFormatNameNewLine);
	constexpr CommandOptionDesc_t s_CommandJiggleBone_Option_AlongStiffness(QC_OPT_FLOAT, "along_stiffness", s_CommandOptionFormatNameNewLine);
	constexpr CommandOptionDesc_t s_CommandJiggleBone_Option_AlongDamping(QC_OPT_FLOAT, "along_damping", s_CommandOptionFormatNameNewLine);
	constexpr CommandOptionDesc_t s_CommandJiggleBone_Option_AllowLengthFlex(QC_OPT_NONE, "allow_length_flex", s_CommandOptionFormatNameNewLine);

	// base spring
	constexpr CommandOptionDesc_t s_CommandJiggleBone_Option_BaseStiffness(QC_OPT_FLOAT, "stiffness", s_CommandOptionFormatNameNewLine);
	constexpr CommandOptionDesc_t s_CommandJiggleBone_Option_BaseDamping(QC_OPT_FLOAT, "damping", s_CommandOptionFormatNameNewLine);
	constexpr CommandOptionDesc_t s_CommandJiggleBone_Option_LeftConstraint(QC_OPT_FLOAT, "left_constraint", s_CommandOptionFormatNameNewLine);
	constexpr CommandOptionDesc_t s_CommandJiggleBone_Option_LeftFriction(QC_OPT_FLOAT, "left_friction", s_CommandOptionFormatNameNewLine);
	constexpr CommandOptionDesc_t s_CommandJiggleBone_Option_UpConstraint(QC_OPT_FLOAT, "up_constraint", s_CommandOptionFormatNameNewLine);
	constexpr CommandOptionDesc_t s_CommandJiggleBone_Option_UpFriction(QC_OPT_FLOAT, "up_friction", s_CommandOptionFormatNameNewLine);
	constexpr CommandOptionDesc_t s_CommandJiggleBone_Option_ForwardConstraint(QC_OPT_FLOAT, "forward_constraint", s_CommandOptionFormatNameNewLine);
	constexpr CommandOptionDesc_t s_CommandJiggleBone_Option_ForwardFriction(QC_OPT_FLOAT, "forward_friction", s_CommandOptionFormatNameNewLine);
	constexpr CommandOptionDesc_t s_CommandJiggleBone_Option_BaseMass(QC_OPT_FLOAT, "base_mass", s_CommandOptionFormatNameNewLine);

	// boing
	constexpr CommandOptionDesc_t s_CommandJiggleBone_Option_BoingImpactSpeed(QC_OPT_FLOAT, "impact_speed", s_CommandOptionFormatNameNewLine, s_QCVersion_TF2, s_QCVersion_TF2);
	constexpr CommandOptionDesc_t s_CommandJiggleBone_Option_BoingImpactAngle(QC_OPT_FLOAT, "impact_angle", s_CommandOptionFormatNameNewLine, s_QCVersion_TF2, s_QCVersion_TF2);
	constexpr CommandOptionDesc_t s_CommandJiggleBone_Option_BoingDampingRate(QC_OPT_FLOAT, "damping_rate", s_CommandOptionFormatNameNewLine, s_QCVersion_TF2, s_QCVersion_TF2);
	constexpr CommandOptionDesc_t s_CommandJiggleBone_Option_BoingFrequency(QC_OPT_FLOAT, "frequency", s_CommandOptionFormatNameNewLine, s_QCVersion_TF2, s_QCVersion_TF2);
	constexpr CommandOptionDesc_t s_CommandJiggleBone_Option_BoingAmplitude(QC_OPT_FLOAT, "amplitude", s_CommandOptionFormatNameNewLine, s_QCVersion_TF2, s_QCVersion_TF2);

	// models
	constexpr CommandOptionDesc_t s_CommandBodyGroup_Option_Studio(QC_OPT_STRING, "studio", s_CommandOptionFormatNameNewLine);
	constexpr CommandOptionDesc_t s_CommandBodyGroup_Option_Blank(QC_OPT_NONE, "blank", s_CommandOptionFormatNameNewLine);

	constexpr CommandOptionDesc_t s_CommandLOD_Option_Threshold(QC_OPT_FLOAT, "threshold");
	constexpr CommandOptionDesc_t s_CommandLOD_Option_ReplaceModel(QC_OPT_PAIR, "replacemodel", s_CommandOptionFormatNameNewLine);
	constexpr CommandOptionDesc_t s_CommandLOD_Option_RemoveModel(QC_OPT_STRING, "removemodel", s_CommandOptionFormatNameNewLine);
	constexpr CommandOptionDesc_t s_CommandLOD_Option_ReplaceBone(QC_OPT_PAIR, "replacebone", s_CommandOptionFormatNameNewLine);
	constexpr CommandOptionDesc_t s_CommandLOD_Option_ShadowLODMaterials(QC_OPT_NONE, "use_shadowlod_materials", s_CommandOptionFormatNameNewLine);

	// animation
	constexpr CommandOptionDesc_t s_CommandPoseParam_Option_Start(QC_OPT_FLOAT, "start");
	constexpr CommandOptionDesc_t s_CommandPoseParam_Option_End(QC_OPT_FLOAT, "end");
	constexpr CommandOptionDesc_t s_CommandPoseParam_Option_Wrap(QC_OPT_NONE, "wrap", s_CommandOptionFormatNameParentLine);
	constexpr CommandOptionDesc_t s_CommandPoseParam_Option_Loop(QC_OPT_FLOAT, "loop", s_CommandOptionFormatNameParentLine);

	constexpr CommandOptionDesc_t s_CommandIKChain_Option_Knee(QC_OPT_FLOAT, "knee", s_CommandOptionFormatNameParentLine);
	constexpr CommandOptionDesc_t s_CommandIKChain_Option_Angle(QC_OPT_FLOAT, "angle_unknown", s_CommandOptionFormatNameParentLine, s_QCVersion_R2, s_QCVersion_R5_RETAIL);

	constexpr CommandOptionDesc_t s_CommandIKAutoPlayLock_Option_LockPosition(QC_OPT_FLOAT, "lock_position");
	constexpr CommandOptionDesc_t s_CommandIKAutoPlayLock_Option_InheritRotation(QC_OPT_FLOAT, "inherit_rotation");

	// collision
	constexpr CommandOptionDesc_t s_CommandContents_Option_Flag(QC_OPT_STRING, "flag");

	constexpr CommandOptionDesc_t s_CommandHBox_Option_Group(QC_OPT_INT, "group");
	constexpr CommandOptionDesc_t s_CommandHBox_Option_Min(QC_OPT_FLOAT, "min");
	constexpr CommandOptionDesc_t s_CommandHBox_Option_Max(QC_OPT_FLOAT, "max");
	constexpr CommandOptionDesc_t s_CommandHBox_Option_Angle(QC_OPT_FLOAT, "angle", s_CommandOptionFormatDefault, s_QCVersion_CSGO, s_QCVersion_CSGO);
	constexpr CommandOptionDesc_t s_CommandHBox_Option_Radius(QC_OPT_FLOAT, "radius", s_CommandOptionFormatDefault, s_QCVersion_CSGO, s_QCVersion_CSGO);
	constexpr CommandOptionDesc_t s_CommandHBox_Option_Crit(QC_OPT_NONE, "force_crit", s_CommandOptionFormatNameParentLine, s_QCVersion_R2, s_QCVersion_R5_150);

	//
	// functions
	//

	// misc
	CommandOption_t* CommandConstDirectLight_ParseBinary(QCFile* const file, uint32_t* const numOptions, const void* const data, const uint32_t count, const bool store = false)
	{
		UNUSED(file);
		UNUSED(count);
		UNUSED(store);

		const uint8_t intensityPacked = reinterpret_cast<const uint8_t* const>(data)[0];

		const uint32_t usedOptions = 1;
		CommandOption_t* options = new CommandOption_t[usedOptions];

		const float intensity = static_cast<float>(intensityPacked) / 255.0f;
		options[0].Init(&s_CommandGeneric_Option_Float);
		options[0].SetRaw(&intensity, 1, sizeof(float));

		*numOptions = usedOptions;

		return options;
	}
	
	CommandOption_t* CommandIllumPosition_ParseBinary(QCFile* const file, uint32_t* const numOptions, const void* const data, const uint32_t count, const bool store = false)
	{
		UNUSED(count);
		UNUSED(store);

		const IllumPositionData_t* const illumdata = reinterpret_cast<const IllumPositionData_t* const>(data);

		const uint32_t usedOptions = illumdata->useAttachment() ? 2 : 1;
		CommandOption_t* options = new CommandOption_t[usedOptions];

		Vector illumposition;
		if (illumdata->useAttachment() && usedOptions > 1u)
		{
			// [rika]: forget if you can index into this type like this
			illumposition.Init(illumdata->localmatrix->m_flMatVal[0][3], illumdata->localmatrix->m_flMatVal[1][3], illumdata->localmatrix->m_flMatVal[2][3]);

			options[1].Init(&s_CommandGeneric_Option_Bone);
			options[1].SetPtr(illumdata->localbone);
		}
		else
		{
			illumposition = *illumdata->illumposition;
			StaticPropFlipFlop(illumposition);
		}

		options[0].Init(&s_CommandGeneric_Option_Pos);
		options[0].SavePtr(file, &illumposition, 3, sizeof(Vector)); // store it so we don't cause the Funny


		*numOptions = usedOptions;

		return options;
	}
	
	CommandOption_t* CommandMaxEyeDeflect_ParseBinary(QCFile* const file, uint32_t* const numOptions, const void* const data, const uint32_t count, const bool store = false)
	{
		UNUSED(file);
		UNUSED(count);
		UNUSED(store);

		const float flMaxEyeDeflection = *reinterpret_cast<const float* const>(data);

		const uint32_t usedOptions = 1;
		CommandOption_t* options = new CommandOption_t[usedOptions];

		const float fixedAngle = RAD2DEG(acosf(flMaxEyeDeflection));
		options[0].Init(&s_CommandGeneric_Option_Float);
		options[0].SetRaw(&fixedAngle, 1u, sizeof(float)); // store it so we don't cause the Funny

		*numOptions = usedOptions;

		return options;
	}

	// for formating keyvalues strings properly
	const size_t CommandKeyvalues_ParseArray(CTextBuffer* const text, const char* const keyvalues, const size_t keyvalueSize, char* const indentation, uint16_t numIndentation)
	{
		assertm(numIndentation < s_QCMaxIndentation, "nested too deeply");
		assertm(keyvalues[0] == '{', "invalid start");

		size_t i = 0ull;

		// seek past any whitespace
		for (; i < keyvalueSize;)
		{
			switch (keyvalues[i])
			{
			case '\t':
			case '\n':
			case ' ':
			case '{':
			{
				i++;
				continue;
			}
			}

			break;
		}

		// entry bracket
		text->WriteFormatted("\n%s{\n", indentation);
		indentation[numIndentation] = '\t';
		numIndentation++;

		bool inStart = true;
		bool inWhitespace = false;
		bool inQuote = false;
		bool inKey = true;
		bool inValue = false;

		// parse each character, one by one. lovely
		for (; i < keyvalueSize;)
		{
			if (keyvalues[i] == '}')
				break;

			assertm(inKey != inValue, "in key and value at the same time?");

			char character = keyvalues[i];

			switch (character)
			{
			case '\"':
			{
				inQuote = !inQuote;
				break;
			}
			case '\t':
			case '\n':
			case ' ':
			{
				if (inQuote)
				{
					break;
				}

				inWhitespace = true;
				i++;
				continue;
			}
			case '{':
			{
				if (inQuote)
				{
					break;
				}

				assertm(inKey && !inValue, "value was key ?");

				i += CommandKeyvalues_ParseArray(text, keyvalues + i, keyvalueSize - i, indentation, numIndentation);

				inStart = true;
				inWhitespace = false;
				inKey = false;
				inValue = true;

				continue;
			}
			}

			if (inWhitespace)
			{
				if (inKey)
				{
					text->WriteCharacter(' ');
				}

				if (inValue)
				{
					text->WriteCharacter('\n');
					inStart = true;
				}

				inWhitespace = false;
				inKey = !inKey;
				inValue = !inValue;
			}

			if (inStart)
			{
				text->WriteString(indentation);
				inStart = false;
			}

			text->WriteCharacter(character);
			i++;
		}

		// exit bracket
		numIndentation--;
		indentation[numIndentation] = '\0';
		text->WriteFormatted("\n%s}", indentation);

		return i + 1;
	}

	// write function for qc because the string requires some post processing
	size_t CommandKeyvalues_Write(const Command_t* const command, char* buffer, size_t bufferSize)
	{
		const CommandInfo_t* const info = command->info;
		const CommandOption_t* const options = command->options;
		const uint32_t numOptions = command->numOptions;

		assertm(info->id == CommandList_t::QC_KEYVALUES, "command was not $keyvalues");
		assertm(numOptions == 1u, "$keyvalues can only have one option");
		assertm(options[0].desc->type == CommandOptionType_t::QC_OPT_STRING, "option was not the correct type");

		const bool formatComment = command->IsComment(); // are we using comment formatting on this command?

		CTextBuffer text(buffer, bufferSize);
		text.SetTextStart();

		// comment out this comand?
		if (formatComment)
		{
			TEXTBUFFER_WRITE_STRING_CT(text, "/*");
		}

		text.WriteString(info->name);

		const char* keyvalues = reinterpret_cast<const char*>(options[0].data.value.ptr);
		size_t keyvalueSize = strnlen_s(keyvalues, bufferSize);

		// skip 'mdlkeyvalue' and any trailing whitespace
		for (size_t i = 0; i < keyvalueSize; i++)
		{
			const char character = keyvalues[i];

			if (character == '\0')
			{
				assertm(false, "contained no entry bracket");

				// comment out this comand?
				if (formatComment)
				{
					TEXTBUFFER_WRITE_STRING_CT(text, "*/");
				}

				text.WriterToText();
				return text.Capacity();
			}

			if (character != '{')
				continue;

			keyvalues += i;
			keyvalueSize -= i;
			break;
		}

		uint16_t numIndentation = 0u;
		constexpr uint16_t indentBufferSize = s_QCMaxIndentation * 2;
		char indentation[indentBufferSize]{};

		CommandKeyvalues_ParseArray(&text, keyvalues, keyvalueSize, indentation, numIndentation);

		// comment out this comand?
		if (formatComment)
		{
			TEXTBUFFER_WRITE_STRING_CT(text, "*/");
		}

		// add a new line
		text.WriteCharacter('\n');

		text.WriterToText();
		return text.Capacity();
	}

	// materials
	CommandOption_t* CommandTextureGroup_ParseBinary(QCFile* const file, uint32_t* const numOptions, const void* const data, const uint32_t count, const bool store = false)
	{
		UNUSED(count);
		UNUSED(store);

		const TextureGroupData_t* const skinData = reinterpret_cast<const TextureGroupData_t* const>(data);
		const bool hasNames = skinData->HasNames();

		const uint32_t usedOptions = skinData->numSkinFamilies + 1 + (hasNames * skinData->numSkinFamilies);
		CommandOption_t* options = new CommandOption_t[usedOptions];

		options[0].Init(&s_CommandGeneric_Option_Name);
		options[0].SetPtr("skinfamilies");

		const char** skinGroup = new const char* [skinData->numIndices];
		const int16_t* skinFamily = skinData->skins;
		const char** skinNames = skinData->names;

		for (uint32_t i = 1; i < usedOptions; i++)
		{
			if (hasNames)
			{
				options[i].Init(&s_CommandTextureGroup_Option_Name);
				options[i].SetPtr(*skinNames);

				skinNames++;
				i++;
			}

			// get the material for a skin
			for (uint32_t indice = 0; indice < skinData->numIndices; indice++)
			{
				const int16_t slot = skinData->indices[indice]; // get the slot of a changed material
				const int16_t material = skinFamily[slot]; // get the material index for a given slot in a skin family

				skinGroup[indice] = skinData->materials[material];
			}

			options[i].Init(&s_CommandTextureGroup_Option_Skin);

			// not an array so just save pointer to that string
			if (skinData->numIndices == 1)
				options[i].SetPtr(skinGroup[0]);
			else
				options[i].SavePtr(file, skinGroup, skinData->numIndices, sizeof(intptr_t) * skinData->numIndices); // store it so we don't cause the Funny

			skinFamily += skinData->numSkinRef;
		}

		FreeAllocArray(skinGroup);

		*numOptions = usedOptions;

		return options;
	}

	// bones
	CommandOption_t* CommandDefineBone_ParseBinary(QCFile* const file, uint32_t* const numOptions, const void* const data, const uint32_t count, const bool store = false)
	{
		UNUSED(count);
		UNUSED(store);

		const BoneData_t* const boneData = reinterpret_cast<const BoneData_t* const>(data);

		const uint32_t usedOptions = boneData->UseFixups() ? 6 : 4;
		CommandOption_t* options = new CommandOption_t[usedOptions];

		options[0].Init(&s_CommandGeneric_Option_Name);
		options[0].SetPtr(boneData->name);

		options[1].Init(&s_CommandDefineBone_Option_Parent);
		options[1].SetPtr(boneData->parent ? boneData->parent : s_CommandDefineBone_Value_Parent);

		options[2].Init(&s_CommandGeneric_Option_Pos);
		options[2].SetPtr(boneData->position, 3);

		const QAngle angles(*boneData->rotation);
		options[3].Init(&s_CommandGeneric_Option_Rot);
		options[3].SavePtr(file, &angles, 3, sizeof(QAngle));

		// [rika]: command doesn't need to have this data, write only if provided
		if (usedOptions >= 6 && boneData->UseFixups())
		{
			Vector fixupPosition;
			QAngle fixupAngles;
			MatrixAngles(*boneData->fixupMatrix, fixupAngles, fixupPosition);

			options[4].Init(&s_CommandDefineBone_Option_FixupPos);
			options[4].SavePtr(file, &fixupPosition, 3, sizeof(Vector));

			options[5].Init(&s_CommandDefineBone_Option_FixupRot);
			options[5].SavePtr(file, &fixupAngles, 3, sizeof(QAngle));
		}

		*numOptions = usedOptions;

		return options;
	}

	CommandOption_t* CommandJointContents_ParseBinary(QCFile* const file, uint32_t* const numOptions, const void* const data, const uint32_t count, const bool store = false)
	{
		UNUSED(file);
		UNUSED(count);
		UNUSED(store);

		const BoneData_t* const boneData = reinterpret_cast<const BoneData_t* const>(data);
		const int contents = boneData->contents;

		int usedFlags = static_cast<int>(__popcnt(contents));

		if (!usedFlags)
			usedFlags = 1;

		const uint32_t usedOptions = static_cast<uint32_t>(usedFlags) + 1;
		CommandOption_t* options = new CommandOption_t[usedOptions];

		// set bone name
		options[0].Init(&s_CommandGeneric_Option_Name);
		options[0].SetPtr(boneData->name);

		// parse contents flags
		for (int i = 0, bitIdx = 0; bitIdx < 32; bitIdx++)
		{
			if (i == usedFlags)
				break;

			// shift here to get the content flag for this bit
			const int content = 1 << bitIdx;
			if (!(contents & content) && contents)
				continue;

			// silly warning h
			if (static_cast<uint32_t>(i + 1) >= usedOptions)
				break;

			options[1 + i].Init(&s_CommandContents_Option_Flag);
			options[1 + i].SetPtr(StudioContentFlagString(contents & content));

			i++;
		}

		*numOptions = usedOptions;

		return options;
	}

	const uint32_t CommandJiggleBone_ParseCommonOptions(const JiggleData_t* const jiggleData, CommandOptionArray_t* const jiggle_property, uint32_t optionsIndex)
	{
		// default of 10.0f so we will write regardless
		jiggle_property->options[optionsIndex].Init(&s_CommandJiggleBone_Option_Length);
		jiggle_property->options[optionsIndex].SetRaw(&jiggleData->length, 1u, sizeof(float));

		// check for a default value
		if (jiggleData->tipMass != 0.0f)
		{
			jiggle_property->options[optionsIndex].Init(&s_CommandJiggleBone_Option_TipMass);
			jiggle_property->options[optionsIndex].SetRaw(&jiggleData->tipMass, 1u, sizeof(float));

			optionsIndex++;
		}

		// check for a default value
		if (jiggleData->tipFriction != 0.0f)
		{
			jiggle_property->options[optionsIndex].Init(&s_CommandJiggleBone_Option_TipFriction);
			jiggle_property->options[optionsIndex].SetRaw(&jiggleData->tipFriction, 1u, sizeof(float));

			optionsIndex++;
		}

		// if JIGGLE_HAS_FLEX is not set on an apex jigglebone
		if (jiggleData->HasNoFlex())
		{
			jiggle_property->options[optionsIndex].Init(&s_CommandJiggleBone_Option_NoFlex);

			optionsIndex++;
		}

		if (jiggleData->HasAngleConstraint() && optionsIndex < jiggle_property->numOptions)
		{
			const float angleLimit = RAD2DEG(jiggleData->angleLimit);
			jiggle_property->options[optionsIndex].Init(&s_CommandJiggleBone_Option_AngleConstraint);
			jiggle_property->options[optionsIndex].SetRaw(&angleLimit, 1u, sizeof(float));

			optionsIndex++;
		}

		// friction and bounce options get read from qc if flag isn't set, but are never used by the game if it isn't, do not bother writing
		if (jiggleData->HasYawConstraint() && optionsIndex < jiggle_property->numOptions)
		{
			const Vector2D constraint(RAD2DEG(jiggleData->minYaw), RAD2DEG(jiggleData->maxYaw));
			jiggle_property->options[optionsIndex].Init(&s_CommandJiggleBone_Option_YawConstraint);
			jiggle_property->options[optionsIndex].SetRaw(&constraint, 2u, sizeof(Vector2D));

			optionsIndex++;

			// check for a default value
			if (jiggleData->yawFriction != 0.0f)
			{
				jiggle_property->options[optionsIndex].Init(&s_CommandJiggleBone_Option_YawFriction);
				jiggle_property->options[optionsIndex].SetRaw(&jiggleData->yawFriction, 1u, sizeof(float));

				optionsIndex++;
			}

			// check for a default value
			if (jiggleData->yawBounce != 0.0f)
			{
				jiggle_property->options[optionsIndex].Init(&s_CommandJiggleBone_Option_YawBounce);
				jiggle_property->options[optionsIndex].SetRaw(&jiggleData->yawBounce, 1u, sizeof(float));

				optionsIndex++;
			}
		}

		// friction and bounce options get read from qc if flag isn't set, but are never used by the game if it isn't, do not bother writing
		if (jiggleData->HasPitchConstraint() && optionsIndex < jiggle_property->numOptions)
		{
			const Vector2D constraint(RAD2DEG(jiggleData->minPitch), RAD2DEG(jiggleData->maxPitch));
			jiggle_property->options[optionsIndex].Init(&s_CommandJiggleBone_Option_PitchConstraint);
			jiggle_property->options[optionsIndex].SetRaw(&constraint, 2u, sizeof(Vector2D)); // takes minYaw and maxYaw

			optionsIndex++;

			// check for a default value
			if (jiggleData->pitchFriction != 0.0f)
			{
				jiggle_property->options[optionsIndex].Init(&s_CommandJiggleBone_Option_PitchFriction);
				jiggle_property->options[optionsIndex].SetRaw(&jiggleData->pitchFriction, 1u, sizeof(float));

				optionsIndex++;
			}

			// check for a default value
			if (jiggleData->pitchBounce != 0.0f)
			{
				jiggle_property->options[optionsIndex].Init(&s_CommandJiggleBone_Option_PitchBounce);
				jiggle_property->options[optionsIndex].SetRaw(&jiggleData->pitchBounce, 1u, sizeof(float));

				optionsIndex++;
			}
		}

		return optionsIndex;
	}

	CommandOption_t* CommandJiggleBone_ParseBinary(QCFile* const file, uint32_t* const numOptions, const void* const data, const uint32_t count, const bool store = false)
	{
		UNUSED(file);
		UNUSED(count);
		UNUSED(store);

		const JiggleData_t* const jiggleData = reinterpret_cast<const JiggleData_t* const>(data);

		const uint32_t usedOptions = 1 + jiggleData->IsFlexible() + jiggleData->IsRigid() + jiggleData->HasBaseSpring() + jiggleData->IsBoing(); // this should be a max of three because is_rigid and is_flexible as well as has_base_spring and is_boing are mutually exclusive
		CommandOption_t* options = new CommandOption_t[usedOptions];

		options[0].Init(&s_CommandGeneric_Option_Bone);
		options[0].SetPtr(jiggleData->bone);

		uint32_t optionsIndex = 1u;

		// is_flexible and is_rigid
		if (jiggleData->IsFlexible() && optionsIndex < usedOptions)
		{
			uint32_t flexibleOptionIndex = 0u;

			const uint32_t numFlexibleOptions = jiggleData->CommonJiggleOptionCount() + jiggleData->FlexibleOptionCount();
			CommandOptionArray_t is_flexible(file, numFlexibleOptions, 1u);

			// stiffness defaults to 100.0f
			if (jiggleData->yawStiffness != 100.0f)
			{
				is_flexible.options[flexibleOptionIndex].Init(&s_CommandJiggleBone_Option_YawStiffness);
				is_flexible.options[flexibleOptionIndex].SetRaw(&jiggleData->yawStiffness, 1u, sizeof(float));

				flexibleOptionIndex++;
			}

			// damping defaults to 0.0f
			if (jiggleData->yawDamping != 0.0f)
			{
				is_flexible.options[flexibleOptionIndex].Init(&s_CommandJiggleBone_Option_YawDamping);
				is_flexible.options[flexibleOptionIndex].SetRaw(&jiggleData->yawDamping, 1u, sizeof(float));

				flexibleOptionIndex++;
			}

			// stiffness defaults to 100.0f
			if (jiggleData->pitchStiffness != 100.0f)
			{
				is_flexible.options[flexibleOptionIndex].Init(&s_CommandJiggleBone_Option_PitchStiffness);
				is_flexible.options[flexibleOptionIndex].SetRaw(&jiggleData->pitchStiffness, 1u, sizeof(float));

				flexibleOptionIndex++;
			}

			// damping defaults to 0.0f
			if (jiggleData->pitchDamping != 0.0f)
			{
				is_flexible.options[flexibleOptionIndex].Init(&s_CommandJiggleBone_Option_PitchDamping);
				is_flexible.options[flexibleOptionIndex].SetRaw(&jiggleData->pitchDamping, 1u, sizeof(float));

				flexibleOptionIndex++;
			}

			if (!jiggleData->HasLengthConstraint())
			{
				// stiffness defaults to 100.0f
				if (jiggleData->alongStiffness != 100.0f)
				{
					is_flexible.options[flexibleOptionIndex].Init(&s_CommandJiggleBone_Option_AlongStiffness);
					is_flexible.options[flexibleOptionIndex].SetRaw(&jiggleData->alongStiffness, 1u, sizeof(float));

					flexibleOptionIndex++;
				}

				// damping defaults to 0.0f
				if (jiggleData->alongDamping != 0.0f)
				{
					is_flexible.options[flexibleOptionIndex].Init(&s_CommandJiggleBone_Option_AlongDamping);
					is_flexible.options[flexibleOptionIndex].SetRaw(&jiggleData->alongDamping, 1u, sizeof(float));

					flexibleOptionIndex++;
				}

				is_flexible.options[flexibleOptionIndex].Init(&s_CommandJiggleBone_Option_AllowLengthFlex);

				flexibleOptionIndex++;
			}

			flexibleOptionIndex = CommandJiggleBone_ParseCommonOptions(jiggleData, &is_flexible, flexibleOptionIndex);

			// truncate because we allow for the max number of jiggle parameters it could have, options are stored in the QCFile's buffer so this is okay
			is_flexible.TruncateOptions(flexibleOptionIndex);

			options[optionsIndex].Init(&s_CommandJiggleBone_Option_IsFlexible);
			options[optionsIndex].SavePtr(file, &is_flexible, 1u, sizeof(CommandOptionArray_t));

			optionsIndex++;
		}
		else if (jiggleData->IsRigid() && optionsIndex < usedOptions)
		{
			uint32_t rigidOptionIndex = 0u;

			CommandOptionArray_t is_rigid(file, jiggleData->CommonJiggleOptionCount(), 1u);
			rigidOptionIndex = CommandJiggleBone_ParseCommonOptions(jiggleData, &is_rigid, rigidOptionIndex);

			// truncate because we allow for the max number of jiggle parameters it could have, options are stored in the QCFile's buffer so this is okay
			is_rigid.TruncateOptions(rigidOptionIndex);

			options[optionsIndex].Init(&s_CommandJiggleBone_Option_IsRigid);
			options[optionsIndex].SavePtr(file, &is_rigid, 1u, sizeof(CommandOptionArray_t));

			optionsIndex++;
		}

		// has_base_spring and is_boing
		if (jiggleData->HasBaseSpring() && optionsIndex < usedOptions)
		{			
			uint32_t springOptionIndex = 0u;

			CommandOptionArray_t has_spring_base(file, jiggleData->BaseSpringOptionCount(), 1u);

			// stiffness defaults to 100.0f
			if (jiggleData->baseStiffness != 100.0f)
			{
				has_spring_base.options[springOptionIndex].Init(&s_CommandJiggleBone_Option_BaseStiffness);
				has_spring_base.options[springOptionIndex].SetRaw(&jiggleData->baseStiffness, 1u, sizeof(float));

				springOptionIndex++;
			}

			// damping defaults to 0.0f
			if (jiggleData->baseDamping != 0.0f)
			{
				has_spring_base.options[springOptionIndex].Init(&s_CommandJiggleBone_Option_BaseDamping);
				has_spring_base.options[springOptionIndex].SetRaw(&jiggleData->baseDamping, 1u, sizeof(float));

				springOptionIndex++;
			}

			// left
			// min defaults to -100.0f and max defaults to 100.0f
			if (jiggleData->baseMinLeft != -100.0f || jiggleData->baseMaxLeft != 100.0f)
			{
				const Vector2D constraint(jiggleData->baseMinLeft, jiggleData->baseMaxLeft);
				has_spring_base.options[springOptionIndex].Init(&s_CommandJiggleBone_Option_LeftConstraint);
				has_spring_base.options[springOptionIndex].SetRaw(&constraint, 1u, sizeof(Vector2D));

				springOptionIndex++;
			}

			// damping defaults to 0.0f
			if (jiggleData->baseLeftFriction != 0.0f)
			{
				has_spring_base.options[springOptionIndex].Init(&s_CommandJiggleBone_Option_LeftFriction);
				has_spring_base.options[springOptionIndex].SetRaw(&jiggleData->baseLeftFriction, 1u, sizeof(float));

				springOptionIndex++;
			}

			// up
			// min defaults to -100.0f and max defaults to 100.0f
			if (jiggleData->baseMinUp != -100.0f || jiggleData->baseMaxUp != 100.0f)
			{
				const Vector2D constraint(jiggleData->baseMinUp, jiggleData->baseMaxUp);
				has_spring_base.options[springOptionIndex].Init(&s_CommandJiggleBone_Option_UpConstraint);
				has_spring_base.options[springOptionIndex].SetRaw(&constraint, 1u, sizeof(Vector2D));

				springOptionIndex++;
			}

			// damping defaults to 0.0f
			if (jiggleData->baseUpFriction != 0.0f)
			{
				has_spring_base.options[springOptionIndex].Init(&s_CommandJiggleBone_Option_UpFriction);
				has_spring_base.options[springOptionIndex].SetRaw(&jiggleData->baseUpFriction, 1u, sizeof(float));

				springOptionIndex++;
			}

			// forward
			// min defaults to -100.0f and max defaults to 100.0f
			if (jiggleData->baseMinForward != -100.0f || jiggleData->baseMaxForward != 100.0f)
			{
				const Vector2D constraint(jiggleData->baseMinForward, jiggleData->baseMaxForward);
				has_spring_base.options[springOptionIndex].Init(&s_CommandJiggleBone_Option_ForwardConstraint);
				has_spring_base.options[springOptionIndex].SetRaw(&constraint, 1u, sizeof(Vector2D));

				springOptionIndex++;
			}

			// damping defaults to 0.0f
			if (jiggleData->baseForwardFriction != 0.0f)
			{
				has_spring_base.options[springOptionIndex].Init(&s_CommandJiggleBone_Option_ForwardFriction);
				has_spring_base.options[springOptionIndex].SetRaw(&jiggleData->baseForwardFriction, 1u, sizeof(float));

				springOptionIndex++;
			}

			// thiccness
			if (jiggleData->baseMass != 0.0f)
			{
				has_spring_base.options[springOptionIndex].Init(&s_CommandJiggleBone_Option_BaseMass);
				has_spring_base.options[springOptionIndex].SetRaw(&jiggleData->baseMass, 1u, sizeof(float));

				springOptionIndex++;
			}

			// truncate because we allow for the max number of jiggle parameters it could have, options are stored in the QCFile's buffer so this is okay
			has_spring_base.TruncateOptions(springOptionIndex);

			options[optionsIndex].Init(&s_CommandJiggleBone_Option_BaseSpring);
			options[optionsIndex].SavePtr(file, &has_spring_base, 1u, sizeof(CommandOptionArray_t));

			optionsIndex++;
		}
		else if (jiggleData->IsBoing() && optionsIndex < usedOptions)
		{
			// 0x0045CC50 studiomdl.exe (teamfortress2)

			uint32_t boingOptionIndex = 0u;

			CommandOptionArray_t is_boing(file, jiggleData->BaseSpringOptionCount(), 1u);

			// impact_speed defaults to 100.0f
			if (jiggleData->boingImpactSpeed != 100.0f)
			{
				is_boing.options[boingOptionIndex].Init(&s_CommandJiggleBone_Option_BoingImpactSpeed);
				is_boing.options[boingOptionIndex].SetRaw(&jiggleData->boingImpactSpeed, 1u, sizeof(float));

				boingOptionIndex++;
			}

			// impact_angle defaults to 0.70709997
			if (jiggleData->boingImpactAngle != 0.70709997f)
			{
				const float boingImpactAngle = RAD2DEG(acosf(jiggleData->boingImpactAngle));
				is_boing.options[boingOptionIndex].Init(&s_CommandJiggleBone_Option_BoingImpactSpeed);
				is_boing.options[boingOptionIndex].SetRaw(&boingImpactAngle, 1u, sizeof(float));

				boingOptionIndex++;
			}

			// damping_rate defaults to 0.25
			if (jiggleData->boingDampingRate != 0.25f)
			{
				is_boing.options[boingOptionIndex].Init(&s_CommandJiggleBone_Option_BoingDampingRate);
				is_boing.options[boingOptionIndex].SetRaw(&jiggleData->boingDampingRate, 1u, sizeof(float));

				boingOptionIndex++;
			}

			// frequency defaults to 30.0
			if (jiggleData->boingFrequency != 30.0f)
			{
				is_boing.options[boingOptionIndex].Init(&s_CommandJiggleBone_Option_BoingFrequency);
				is_boing.options[boingOptionIndex].SetRaw(&jiggleData->boingFrequency, 1u, sizeof(float));

				boingOptionIndex++;
			}

			// amplitude defaults to 0.34999999
			if (jiggleData->boingAmplitude != 0.34999999f)
			{
				is_boing.options[boingOptionIndex].Init(&s_CommandJiggleBone_Option_BoingAmplitude);
				is_boing.options[boingOptionIndex].SetRaw(&jiggleData->boingAmplitude, 1u, sizeof(float));

				boingOptionIndex++;
			}

			// truncate because we allow for the max number of jiggle parameters it could have, options are stored in the QCFile's buffer so this is okay
			is_boing.TruncateOptions(boingOptionIndex);

			options[optionsIndex].Init(&s_CommandJiggleBone_Option_IsBoing);
			options[optionsIndex].SavePtr(file, &is_boing, 1u, sizeof(CommandOptionArray_t));

			optionsIndex++;

			optionsIndex++;
		}

		*numOptions = usedOptions;

		return options;
	}

	CommandOption_t* CommandAttachment_ParseBinary(QCFile* const file, uint32_t* const numOptions, const void* const data, const uint32_t count, const bool store = false)
	{
		UNUSED(count);
		UNUSED(store);

		const AttachmentData_t* const attachmentData = reinterpret_cast<const AttachmentData_t* const>(data);

		// name, bone, pos, type (+ rotation if rotate type)
		const uint32_t usedOptions = 4 + (!attachmentData->WorldAlign() ? 1 : 0);
		CommandOption_t* options = new CommandOption_t[usedOptions];

		// set name
		options[0].Init(&s_CommandGeneric_Option_Name);
		options[0].SetPtr(attachmentData->name);

		// set bone
		options[1].Init(&s_CommandGeneric_Option_Bone);
		options[1].SetPtr(attachmentData->localbone);

		// do these need to be altered for static prop ? studiomdl seems to recalc them if the model is a static prop
		Vector pos;
		QAngle rot;
		MatrixAngles(*attachmentData->localmatrix, rot, pos);

		// set pos
		options[2].Init(&s_CommandGeneric_Option_Pos);
		options[2].SavePtr(file, &pos, 3, sizeof(Vector));

		// set type
		// todo: absolute and maybe rigid detecting? absolute probably possible, rigid eeehhh
		const AttachmentData_t::Type_t type = attachmentData->WorldAlign() ? AttachmentData_t::Type_t::ATTACH_WORLD_ALIGN : AttachmentData_t::Type_t::ATTACH_ROTATE;

		options[3].Init(&s_CommandAttachment_Option_Type);
		options[3].SetPtr(s_AttachmentTypeNames[type]);

		if (type == AttachmentData_t::Type_t::ATTACH_ROTATE && usedOptions >= 5)
		{
			// valve devwiki states that these are "weird", and crowbar's parsing of this reflects that. gets adjusted a lot in studiomdl too.
			// however, the output of rot seems to match crowbar's shifting (rot.y, -rot.z, -rot.x), perhaps zeq's MatrixAngles function is off?

			options[4].Init(&s_CommandGeneric_Option_Rot);
			options[4].SavePtr(file, &rot, 3, sizeof(QAngle));
		}

		*numOptions = usedOptions;

		return options;
	}

	// models
	CommandOption_t* CommandBodyGroup_ParseBinary(QCFile* const file, uint32_t* const numOptions, const void* const data, const uint32_t count, const bool store = false)
	{
		UNUSED(count);

		const BodyGroupData_t* const bodyGroupData = reinterpret_cast<const BodyGroupData_t* const>(data);

		const uint32_t usedOptions = bodyGroupData->numParts + 1;
		CommandOption_t* options = new CommandOption_t[usedOptions];

		options[0].Init(&s_CommandGeneric_Option_Name);

		if (store)
			options[0].SaveStr(file, bodyGroupData->name);
		else
			options[0].SetPtr(bodyGroupData->name);

		uint32_t optionsIndex = 1;

		for (uint32_t i = 0; i < bodyGroupData->numParts; i++)
		{
			if (optionsIndex + i >= usedOptions)
				break;

			// model uses 'blank' option
			if (!bodyGroupData->parts[i])
			{
				options[optionsIndex + i].Init(&s_CommandBodyGroup_Option_Blank);
				continue;
			}

			// model uses 'studio' option
			options[optionsIndex + i].Init(&s_CommandBodyGroup_Option_Studio);

			if (store)
			{
				options[optionsIndex + i].SaveStr(file, bodyGroupData->parts[i]);
				continue;
			}

			options[optionsIndex + i].SetPtr(bodyGroupData->parts[i]);
		}

		*numOptions = usedOptions;

		return options;
	}

	CommandOption_t* CommandLOD_ParseBinary(QCFile* const file, uint32_t* const numOptions, const void* const data, const uint32_t count, const bool store = false)
	{
		UNUSED(count);

		const LodData_t* const lodGroupData = reinterpret_cast<const LodData_t* const>(data);

		const uint32_t usedOptions = lodGroupData->numModels + lodGroupData->numBoneUsed + lodGroupData->UseThreshold() + lodGroupData->useShadowLODMaterials;
		CommandOption_t* options = new CommandOption_t[usedOptions];

		uint32_t optionsIndex = 0;

		if (lodGroupData->UseThreshold())
		{
			options[0].Init(&s_CommandLOD_Option_Threshold);
			options[0].SetRaw(&lodGroupData->threshold, 1, sizeof(float));

			optionsIndex++;
		}

		for (uint32_t i = 0; i < lodGroupData->numModels; i++)
		{
			if (optionsIndex + i >= usedOptions)
				break;

			const char* const baseName = lodGroupData->modelBaseNames[i];
			const char* const replaceName = lodGroupData->modelReplaceNames[i];

			// model is ignored
			if (!baseName)
			{
				options[optionsIndex + i].Init(&s_CommandGeneric_Option_None);
				continue;
			}

			// model is removed
			if (!replaceName)
			{
				options[optionsIndex + i].Init(&s_CommandLOD_Option_RemoveModel);

				if (store)
					options[optionsIndex + i].SaveStr(file, baseName);
				else
					options[optionsIndex + i].SetPtr(baseName);

				continue;
			}

			// model is replaced
			options[optionsIndex + i].Init(&s_CommandLOD_Option_ReplaceModel);

			if (store)
			{
				CommandOptionPair_t replaceModel(file, baseName, replaceName);
				options[optionsIndex + i].SavePtr(file, &replaceModel, 1, sizeof(CommandOptionPair_t));
			}
			else
			{
				CommandOptionPair_t replaceModel(baseName, replaceName);
				options[optionsIndex + i].SavePtr(file, &replaceModel, 1, sizeof(CommandOptionPair_t));
			}
		}

		optionsIndex += lodGroupData->numModels;

		for (uint32_t i = 0, slot = 0; slot < lodGroupData->numBoneSlots; slot++)
		{
			if (optionsIndex + i >= usedOptions)
				break;

			const char* const baseName = lodGroupData->boneBaseNames[slot];
			const char* const replaceName = lodGroupData->boneReplaceNames[slot];

			// bone is ignored
			if (!baseName || !replaceName)
				continue;

			// bone is replaced
			options[optionsIndex + i].Init(&s_CommandLOD_Option_ReplaceBone);

			if (store)
			{
				CommandOptionPair_t replaceBone(file, baseName, replaceName);
				options[optionsIndex + i].SavePtr(file, &replaceBone, 1, sizeof(CommandOptionPair_t));
			}
			else
			{
				CommandOptionPair_t replaceBone(baseName, replaceName);
				options[optionsIndex + i].SavePtr(file, &replaceBone, 1, sizeof(CommandOptionPair_t));
			}

			i++;
		}

		optionsIndex += lodGroupData->numBoneUsed;

		// to be tested
		if (lodGroupData->useShadowLODMaterials)
			options[optionsIndex].Init(&s_CommandLOD_Option_ShadowLODMaterials);

		*numOptions = usedOptions;

		return options;
	}

	// animations
	CommandOption_t* CommandPoseParam_ParseBinary(QCFile* const file, uint32_t* const numOptions, const void* const data, const uint32_t count, const bool store = false)
	{
		UNUSED(count);

		const PoseParamData_t* const poseParamData = reinterpret_cast<const PoseParamData_t* const>(data);

		const uint32_t usedOptions = 3u + poseParamData->IsLooped();
		CommandOption_t* options = new CommandOption_t[usedOptions];

		options[0].Init(&s_CommandGeneric_Option_Name);

		if (store)
			options[0].SaveStr(file, poseParamData->name);
		else
			options[0].SetPtr(poseParamData->name);

		if (usedOptions >= 3)
		{
			options[1].Init(&s_CommandPoseParam_Option_Start);
			options[1].SetRaw(&poseParamData->start, 1, sizeof(float));

			options[2].Init(&s_CommandPoseParam_Option_End);
			options[2].SetRaw(&poseParamData->end, 1, sizeof(float));
		}

		if (poseParamData->IsLooped() && usedOptions >= 4)
		{
			if (poseParamData->IsWrapped())
			{
				options[3].Init(&s_CommandPoseParam_Option_Wrap);
			}
			else
			{
				options[3].Init(&s_CommandPoseParam_Option_Loop);
				options[3].SetRaw(&poseParamData->loop, 1, sizeof(float));
			}
		}

		*numOptions = usedOptions;

		return options;
	}

	CommandOption_t* CommandIKChain_ParseBinary(QCFile* const file, uint32_t* const numOptions, const void* const data, const uint32_t count, const bool store = false)
	{
		UNUSED(count);

		const IKChainData_t* const ikChainData = reinterpret_cast<const IKChainData_t* const>(data);

		const bool hasKnee = ikChainData->UseKnee();

		const uint32_t usedOptions = 3u + hasKnee; // name, bone, [knee], [angle]
		CommandOption_t* options = new CommandOption_t[usedOptions];

		options[0].Init(&s_CommandGeneric_Option_Name);

		if (store)
			options[0].SaveStr(file, ikChainData->name);
		else
			options[0].SetPtr(ikChainData->name);

		uint32_t optionsIndex = 1u;

		// bone
		if (usedOptions > optionsIndex)
		{
			options[optionsIndex].Init(&s_CommandGeneric_Option_Bone);

			if (store)
				options[optionsIndex].SaveStr(file, ikChainData->bone);
			else
				options[optionsIndex].SetPtr(ikChainData->bone);

			optionsIndex++;
		}

		// knee
		if (hasKnee && usedOptions > optionsIndex)
		{
			options[optionsIndex].Init(&s_CommandIKChain_Option_Knee);

			if (store)
				options[optionsIndex].SavePtr(file, ikChainData->knee, 3u, sizeof(Vector));
			else
				options[optionsIndex].SetPtr(ikChainData->knee, 3u);

			optionsIndex++;
		}

		// weird angle
		if (usedOptions > optionsIndex)
		{
			const float angleQuirky = RAD2DEG(acosf(ikChainData->angleUnknown));
			options[optionsIndex].Init(&s_CommandIKChain_Option_Angle);
			options[optionsIndex].SetRaw(&angleQuirky, 1, sizeof(float));

			optionsIndex++;
		}

		*numOptions = usedOptions;

		return options;
	}

	CommandOption_t* CommandIKAutoPlayLock_ParseBinary(QCFile* const file, uint32_t* const numOptions, const void* const data, const uint32_t count, const bool store = false)
	{
		UNUSED(count);

		const IKLockData_t* const ikLockData = reinterpret_cast<const IKLockData_t* const>(data);

		const uint32_t usedOptions = 3u;
		CommandOption_t* options = new CommandOption_t[usedOptions];

		options[0].Init(&s_CommandGeneric_Option_Name);

		if (store)
			options[0].SaveStr(file, ikLockData->chain);
		else
			options[0].SetPtr(ikLockData->chain);

		options[1].Init(&s_CommandIKAutoPlayLock_Option_LockPosition);
		options[1].SetRaw(&ikLockData->flPosWeight, 1, sizeof(float));

		options[2].Init(&s_CommandIKAutoPlayLock_Option_InheritRotation);
		options[2].SetRaw(&ikLockData->flLocalQWeight, 1, sizeof(float));

		*numOptions = usedOptions;

		return options;
	}

	// collision
	CommandOption_t* CommandContents_ParseBinary(QCFile* const file, uint32_t* const numOptions, const void* const data, const uint32_t count, const bool store = false)
	{
		UNUSED(file);
		UNUSED(count);
		UNUSED(store);

		const int contents = *reinterpret_cast<const int* const>(data);

		int usedFlags = static_cast<int>(__popcnt(contents));

		if (!usedFlags)
			usedFlags = 1;

		CommandOption_t* options = new CommandOption_t[usedFlags];

		for (int i = 0, bitIdx = 0; bitIdx < 32; bitIdx++)
		{
			if (i == usedFlags)
				break;

			// shift here to get the content flag for this bit
			const int content = 1 << bitIdx;
			if (!(contents & content) && contents)
				continue;

			options[i].Init(&s_CommandContents_Option_Flag);
			options[i].SetPtr(StudioContentFlagString(contents & content));

			i++;
		}

		*numOptions = usedFlags;

		return options;
	}

	CommandOption_t* CommandHBox_ParseBinary(QCFile* const file, uint32_t* const numOptions, const void* const data, const uint32_t count, const bool store = false)
	{
		UNUSED(count);
		UNUSED(store);

		const Hitbox_t* const hitboxGroupData = reinterpret_cast<const Hitbox_t* const>(data);

		// group, bone, min, max, angle (csgo), radius (csgo), force crit (r2), name (optional)
		assertm(hitboxGroupData->forceCritPoint < 2, "invalid forceCritPoint value");
		const bool isCSGO = hitboxGroupData->IsCSGO();
		const bool hasName = hitboxGroupData->HasName();
		const uint32_t usedOptions = 4 + (isCSGO ? 2 : 0) + hasName + hitboxGroupData->forceCritPoint;
		CommandOption_t* options = new CommandOption_t[usedOptions];

		uint32_t optionsIndex = 0;

		if ((optionsIndex + 4) <= usedOptions)
		{
			options[0].Init(&s_CommandHBox_Option_Group);
			options[0].SetRaw(&hitboxGroupData->group, 1, sizeof(int));

			options[1].Init(&s_CommandGeneric_Option_Name);
			options[1].SetPtr(hitboxGroupData->bone);

			options[2].Init(&s_CommandHBox_Option_Min);
			options[2].SetPtr(hitboxGroupData->bbmin, 3);

			options[3].Init(&s_CommandHBox_Option_Max);
			options[3].SetPtr(hitboxGroupData->bbmax, 3);

			optionsIndex += 4;
		}

		if (isCSGO && (optionsIndex + 2) <= usedOptions)
		{
			QAngle ang(hitboxGroupData->bbangle->z, hitboxGroupData->bbangle->x, hitboxGroupData->bbangle->y);
			options[optionsIndex].Init(&s_CommandHBox_Option_Angle);
			options[optionsIndex].SavePtr(file, &ang, 3, sizeof(QAngle));

			options[optionsIndex + 1].Init(&s_CommandHBox_Option_Radius);
			options[optionsIndex + 1].SetRaw(&hitboxGroupData->flRadius);

			optionsIndex += 2;
		}

		if (hasName && (optionsIndex + 1) <= usedOptions)
		{
			options[optionsIndex].Init(&s_CommandGeneric_Option_Name);
			options[optionsIndex].SetPtr(hitboxGroupData->name);

			optionsIndex += 1;
		}

		if (hitboxGroupData->forceCritPoint && (optionsIndex + 1) <= usedOptions)
		{
			options[optionsIndex].Init(&s_CommandHBox_Option_Crit);

			optionsIndex += 1;
		}

		*numOptions = usedOptions;

		return options;
	}


	//
	// COMMAND SORTING
	//

	int CompareCommand(const void* a, const void* b)
	{
		const Command_t* const cmda = *reinterpret_cast<const Command_t* const* const>(a);
		const Command_t* const cmdb = *reinterpret_cast<const Command_t* const* const>(b);

		if (cmda->GetCmd() < cmdb->GetCmd())
			return -1;

		if (cmda->GetCmd() > cmdb->GetCmd())
			return 1;

		return 0;
	}

	int CompareCommandLOD(const void* a, const void* b)
	{
		const Command_t* const cmda = *reinterpret_cast<const Command_t* const* const>(a);
		const Command_t* const cmdb = *reinterpret_cast<const Command_t* const* const>(b);

		float thresholda = 0.0f;
		float thresholdb = 0.0f;

		for (uint32_t i = 0; i < cmda->numOptions; i++)
		{
			const CommandOption_t* const option = cmda->options + i;
			if (option->desc != &s_CommandLOD_Option_Threshold)
				continue;

			thresholda = *reinterpret_cast<const float* const>(&option->data.value.raw);
			break;
		}

		for (uint32_t i = 0; i < cmdb->numOptions; i++)
		{
			const CommandOption_t* const option = cmdb->options + i;
			if (option->desc != &s_CommandLOD_Option_Threshold)
				continue;

			thresholdb = *reinterpret_cast<const float* const>(&option->data.value.raw);
			break;
		}

		// todo: test!!!
		// sort shadowlod last
		if (thresholda == -1.0f)
			return 1;

		if (thresholda < thresholdb)
			return -1;

		if (thresholda > thresholdb)
			return 1;

		return 0;
	}

	const uint32_t CommandSortDefault(const Command_t* const commands, const Command_t** const sorted, const size_t numCommands, const CommandType_t type)
	{
		uint32_t numSorted = 0u;

		for (size_t i = 0ull; i < numCommands; i++)
		{
			const Command_t* const command = commands + i;

			if (command->GetType() != type)
				continue;

			sorted[numSorted] = command;
			numSorted++;
		}

		if (numSorted == 0u)
			return numSorted;

		qsort(sorted, numSorted, sizeof(intptr_t), CompareCommand);

		return numSorted;
	}

	inline const bool CommadSortModel_UnsortedCMD(const CommandList_t cmd)
	{
		return (cmd == CommandList_t::QC_BODY || cmd == CommandList_t::QC_BODYGROUP || cmd == CommandList_t::QC_MODEL);
	}

	const uint32_t CommandSortModel(const Command_t* const commands, const Command_t** const sorted, const size_t numCommands, const CommandType_t type)
	{
		uint32_t numSorted = 0u;
		uint32_t numGeneralSorted = 0u;

		for (size_t i = 0ull; i < numCommands; i++)
		{
			const Command_t* const command = commands + i;

			if (command->GetType() != type)
				continue;

			if (!CommadSortModel_UnsortedCMD(command->GetCmd()))
			{
				numGeneralSorted++;
			}

			sorted[numSorted] = command;
			numSorted++;
		}

		// we want to preserve order, and don't need to parse through again if $maxverts and other commands are not present
		if (numGeneralSorted == 0u)
			return numSorted;

		const Command_t** const temp = sorted + numSorted;
		memcpy_s(temp, sizeof(intptr_t) * numSorted, sorted, sizeof(intptr_t) * numSorted);

		for (uint32_t sortIdx = 0u, copyIdx = numGeneralSorted, i = 0u; i < numSorted; i++)
		{
			const Command_t* const command = temp[i];

			if (!CommadSortModel_UnsortedCMD(command->GetCmd()))
			{
				assertm(sortIdx < numSorted, "invalid index");

				sorted[sortIdx] = temp[i];
				sortIdx++;
				continue;
			}

			assertm(copyIdx < numSorted, "invalid index");

			sorted[copyIdx] = temp[i];
			copyIdx++;
		}

		qsort(sorted, numGeneralSorted, sizeof(intptr_t), CompareCommand);

		return numSorted;
	}

	const uint32_t CommandSortLOD(const Command_t* const commands, const Command_t** const sorted, const size_t numCommands, const CommandType_t type)
	{
		uint32_t numSorted = 0u;

		for (size_t i = 0ull; i < numCommands; i++)
		{
			const Command_t* const command = commands + i;

			if (command->GetType() != type)
				continue;

			sorted[numSorted] = command;
			numSorted++;
		}

		// we want to preserve order, and don't need to parse through again if $maxverts is not present
		if (numSorted == 0)
			return numSorted;

		qsort(sorted, numSorted, sizeof(intptr_t), CompareCommandLOD);

		return numSorted;
	}

	const uint32_t CommandSortBox(const Command_t* const commands, const Command_t** const sorted, const size_t numCommands, const CommandType_t type)
	{
		uint32_t numSorted = 0u;

		for (size_t i = 0ull; i < numCommands; i++)
		{
			const Command_t* const command = commands + i;

			if (command->GetType() != type)
				continue;

			sorted[numSorted] = command;
			numSorted++;
		}

		// we want to preserve order, and don't need to parse through again if $maxverts is not present
		if (numSorted == 0)
			return numSorted;

		const Command_t** const temp = sorted + numSorted;
		memcpy_s(temp, sizeof(intptr_t) * numSorted, sorted, sizeof(intptr_t) * numSorted);

		uint32_t sortIdx = 0u;
		for (uint32_t tmpIdx = 0u; tmpIdx < numSorted; tmpIdx++)
		{
			const CommandList_t cmd = temp[tmpIdx]->GetCmd();
			if (cmd == CommandList_t::QC_HBOXSET || cmd == CommandList_t::QC_HBOX)
				continue;

			sorted[sortIdx] = temp[tmpIdx];
			sortIdx++;
		}

		qsort(sorted, sortIdx, sizeof(intptr_t), CompareCommand);

		for (uint32_t tmpIdx = 0u; sortIdx < numSorted; tmpIdx++)
		{
			const CommandList_t cmd = temp[tmpIdx]->GetCmd();
			if (cmd != CommandList_t::QC_HBOXSET && cmd != CommandList_t::QC_HBOX)
				continue;

			sorted[sortIdx] = temp[tmpIdx];
			sortIdx++;
		}

		return numSorted;
	}
}