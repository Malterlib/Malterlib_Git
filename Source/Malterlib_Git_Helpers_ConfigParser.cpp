// Copyright © 2024 Unbroken AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Git_Helpers_ConfigParser.h"

namespace NMib::NGit
{
	bool CGitConfigParser::fs_ToBoolean(CStr const &_String)
	{
		if (_String == "yes")
			return true;

		if (_String == "on")
			return true;

		if (_String == "true")
			return true;

		if (_String == "1")
			return true;

		return false;
	}

	TCVector<CStr> const *CGitConfigParser::f_GetValues(CStr const &_Section, CStr const &_SubSection, CStr const &_Key) const
	{
		auto *pSection = m_Sections.f_FindEqual(_Section);
		if (!pSection)
			return nullptr;

		auto *pSubSection = pSection->m_SubSections.f_FindEqual(_SubSection);
		if (!pSubSection)
			return nullptr;

		auto *pValues = pSubSection->m_Values.f_FindEqual(_Key);
		if (!pValues)
			return nullptr;

		return pValues;
	}

	TCVector<CStr> const *CGitConfigParser::f_GetValues(CStr const &_Section, CStr const &_Key) const
	{
		return f_GetValues(_Section, CStr(), _Key);
	}

	CStr const *CGitConfigParser::f_GetValue(CStr const &_Section, CStr const &_SubSection, CStr const &_Key) const
	{
		auto *pValues = f_GetValues(_Section, _SubSection, _Key);
		if (!pValues)
			return nullptr;

		return &pValues->f_GetLast();
	}

	CStr const *CGitConfigParser::f_GetValue(CStr const &_Section, CStr const &_Key) const
	{
		return f_GetValue(_Section, CStr(), _Key);
	}

	static bool fg_ParseComment(ch8 const *&_pParse)
	{
		if (*_pParse != '#' && *_pParse != ';')
			return false;

		fg_ParseToEndOfLine(_pParse);

		return true;
	}

	static void fg_ParseEscape(ch8 const *&_pParse)
	{
		auto *pParse = _pParse;
		auto *pParseStartFunction = pParse;
		int Mode = 0;
		while (*pParse)
		{
			if (Mode == 0)
			{
				_pParse = pParse;
				if (fg_ParseComment(pParse))
				{
					if (_pParse > pParseStartFunction)
					{
						while (_pParse > pParseStartFunction && fg_CharIsWhiteSpace(_pParse[-1]))
							--_pParse;
					}

					return;
				}

				if (fg_CharIsNewLine(*pParse))
					break;
				else if (*pParse == '"')
				{
					Mode = 1;
					++pParse;

					continue;
				}
			}
			else if (Mode == 1)
			{
				if (fg_CharIsNewLine(*pParse))
					break;
				else if (*pParse == '"')
				{
					Mode = 0;
					++pParse;

					continue;
				}
				if (*pParse == '\\')
				{
					++pParse;
					if (*pParse)
						++pParse;

					continue;
				}
			}
			++pParse;
		}

		_pParse = pParse;
	}

	static CStr fg_RemoveEscape(CStr const &_Str)
	{
		CStr Ret;
		auto const *pParse = _Str.f_GetStr();
		int Mode = 0;
		while (*pParse)
		{
			if (Mode == 0)
			{
				if (*pParse == '"')
				{
					Mode = 1;
					++pParse;

					continue;
				}
			}
			else if (Mode == 1)
			{
				if (*pParse == '"')
				{
					Mode = 0;
					++pParse;

					continue;
				}
				if (*pParse == '\\')
				{
					++pParse;
					if (*pParse)
					{
						if (*pParse == 'n')
							Ret.f_AddChar('\n');
						else if (*pParse == 't')
							Ret.f_AddChar('\t');
						else if (*pParse == 'b')
							Ret.f_AddChar('\b');
						else
							Ret.f_AddChar(*pParse);
						++pParse;
					}

					continue;
				}
			}
			Ret.f_AddChar(*pParse);
			++pParse;
		}

		return Ret;
	}

	CGitConfigParser CGitConfigParser::fs_Parse(CStr const &_String)
	{
		CGitConfigParser Parser;

		auto *pParse = _String.f_GetStr();

		CStr Section;
		CStr SubSection;

		while (*pParse)
		{
			fg_ParseWhiteSpace(pParse);

			if (*pParse == '[')
			{
				++pParse;
				fg_ParseWhiteSpaceNoLines(pParse);

				auto *pParseStart = pParse;
				fg_ParseAlphaNumericAndChars(pParse, "-.");

				Section = CStr(CInitByRange(), pParseStart, pParse).f_LowerCase();
				SubSection = CStr();

				fg_ParseWhiteSpaceNoLines(pParse);

				if (*pParse == '"')
				{
					pParseStart = pParse;
					NStr::fg_ParseEscape<'"'>(pParse, '"');

					SubSection = fg_RemoveEscape(CStr(CInitByRange(), pParseStart, pParse));
				}

				while(*pParse && *pParse != ']' && !fg_CharIsNewLine(*pParse))
					++pParse;

				continue;
			}

			if (fg_ParseComment(pParse))
				continue;

			if (!fg_CharIsAlphabetical(*pParse))
			{
				fg_ParseToEndOfLine(pParse);

				continue;
			}

			auto *pParseStart = pParse;
			fg_ParseAlphaNumericAndChars(pParse, "-");
			auto KeyName = CStr(CInitByRange(), pParseStart, pParse).f_LowerCase();

			fg_ParseWhiteSpace(pParse);

			if (*pParse != '=')
			{
				fg_ParseToEndOfLine(pParse);

				continue;
			}

			++pParse;
			fg_ParseWhiteSpace(pParse);

			CStr Value;

			pParseStart = pParse;
			fg_ParseEscape(pParse);
			Value = fg_RemoveEscape(CStr(CInitByRange(), pParseStart, pParse));
			while (*pParse && fg_CharIsNewLine(*pParse) && pParse[-1] == '\\')
			{
				Value = Value.f_RemoveSuffix("\\");
				fg_ParseEndOfLine(pParse);
				fg_ParseWhiteSpaceNoLines(pParse);
				pParseStart = pParse;
				fg_ParseEscape(pParse);
				Value += fg_RemoveEscape(CStr(CInitByRange(), pParseStart, pParse));
			}

			Parser.m_Sections[Section].m_SubSections[SubSection].m_Values[KeyName].f_Insert(fg_Move(Value));
		}

		return Parser;
	}
}
