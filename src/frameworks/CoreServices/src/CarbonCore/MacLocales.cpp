/*
This file is part of Darling.

Copyright (C) 2012-2013 Lubos Dolezel

Darling is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Darling is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Darling.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <CoreServices/MacLocales.h>
#include <CoreServices/MacErrors.h>
#include <unicode/locid.h>
#include <unicode/coll.h>
#include <cstring>
#include <algorithm>
#include <errno.h>
#include <cassert>
#include <string>
#include <map>
#include <pthread.h>

static std::map<std::string,int> g_mapLocaleString;
static std::map<int,std::string> g_mapLocaleStringRev;
static pthread_mutex_t g_mapLocaleStringMutex = PTHREAD_MUTEX_INITIALIZER;

namespace Darling
{

int getLocaleUID(const std::string& str)
{
	auto it = g_mapLocaleString.find(str);
	if (it != g_mapLocaleString.end())
		return it->second;
	else
	{
		pthread_mutex_lock(&g_mapLocaleStringMutex);
		size_t id = g_mapLocaleString.size()+1;
		
		g_mapLocaleString[str] = id;
		g_mapLocaleStringRev[id] = str;
		pthread_mutex_unlock(&g_mapLocaleStringMutex);
		
		return id;
	}
}

const char* getLocaleString(int uid)
{
	auto it = g_mapLocaleStringRev.find(uid);
	if (it != g_mapLocaleStringRev.end())
		return it->second.c_str();
	else
		return "INVALID";
}

}

using namespace Darling;

OSStatus LocaleRefFromLangOrRegionCode(LangCode langCode, RegionCode regionCode, LocaleRef* refOut)
{
	if (langCode == kTextLanguageDontCare || regionCode == kTextRegionDontCare)
	{
		*refOut = getLocaleUID(Locale::getDefault().getName());
		return 0;
	}

	char lc[3], rc[3];
	lc[0] = char(langCode & 255);
	lc[1] = char(langCode >> 8);
	lc[2] = 0;
	rc[0] = char(regionCode & 255);
	rc[1] = char(regionCode >> 8);
	rc[2] = 0;

	Locale loc(lc, rc);
	if (loc.isBogus())
	{
		*refOut = 0;
		return makeOSStatus(ENOENT);
	}
	else
	{
		*refOut = getLocaleUID(loc.getName());
		return 0;
	}
}

OSStatus LocaleRefFromLocaleString(const char* str, LocaleRef* refOut)
{
	Locale loc(str);
	if (loc.isBogus())
	{
		*refOut = 0;
		return makeOSStatus(EINVAL);
	}
	else
	{
		*refOut = getLocaleUID(loc.getBaseName());
		return 0;
	}
}

OSStatus LocaleRefGetPartString(LocaleRef ref, uint32_t partMask, unsigned long maxStringLen, char* stringOut)
{
	Locale loc(getLocaleString(ref));
	char buffer[50] = "";

	if (loc.isBogus())
	{
		if (maxStringLen)
			*stringOut = 0;
		return makeOSStatus(EINVAL);
	}

	if (partMask & (kLocaleLanguageMask|kLocaleLanguageVariantMask))
		strcpy(buffer, loc.getLanguage());
	if (partMask & (kLocaleRegionMask|kLocaleRegionVariantMask))
	{
		strcat(buffer, "_");
		strcat(buffer, loc.getCountry());
	}
	if (partMask & (kLocaleScriptMask|kLocaleScriptVariantMask))
	{
		strcat(buffer, "@");
		strcat(buffer, loc.getScript());
	}

	strncpy(stringOut, buffer, maxStringLen-1);
	stringOut[maxStringLen-1] = 0;
	return 0;
}

OSStatus LocaleStringToLangAndRegionCodes(const char* name, LangCode* langCode, RegionCode* regionCode)
{
	Locale loc(name);
	if (loc.isBogus())
	{
		*langCode = kTextLanguageDontCare;
		*regionCode = kTextRegionDontCare;
		return makeOSStatus(ENOENT);
	}

	const char* lang = loc.getLanguage();
	const char* region = loc.getCountry();
	*langCode = LangCode(*lang) | (LangCode(lang[1]) << 8);
	*regionCode = RegionCode(*region) | (RegionCode(region[1]) << 8);

	return 0;
}

OSStatus LocaleOperationCountLocales(LocaleOperationClass cls, unsigned long* count)
{
	if (cls != 'ucol')
		*count = 0;
	else
	{
		int32_t cc;
		Collator::getAvailableLocales(cc);
		*count = cc;
	}
	return 0;
}

OSStatus LocaleOperationGetLocales(LocaleOperationClass cls, unsigned long max, unsigned long* countOut, LocaleAndVariant* out)
{
	if (cls != 'ucol')
		*countOut = 0;
	else
	{
		int32_t cc;
		const Locale* locales = Collator::getAvailableLocales(cc);

		for (int i = 0; i < cc && i < max; i++)
		{
			out[i].ref = getLocaleUID(locales[i].getName());
			out[i].variant = 0;
		}
		*countOut = std::min<int32_t>(cc, max);
	}
	return 0;
}

OSStatus LocaleGetName(LocaleRef ref, LocaleOperationVariant variant, uint32_t nameMask,
	LocaleRef refDisplay, unsigned long maxLen, unsigned long* lenOut, Utf16Char* displayName)
{
	Locale loc(getLocaleString(ref));
	Locale locDisplay(getLocaleString(refDisplay));

	// variant and nameMask currently ignored
	
	size_t r;
	UnicodeString str, str2;
	loc.getDisplayLanguage(locDisplay, str);
	str += " (";
	loc.getDisplayCountry(locDisplay, str2);
	str += str2;
	str += ")";
	
	int32_t req = str.extract(0, str.length(), (char*) displayName, maxLen * sizeof(Utf16Char), "UTF-16");
	*lenOut = req / sizeof(Utf16Char);
	
	if (req > maxLen * sizeof(Utf16Char))
		return kLocalesBufferTooSmallErr;
	else
		return noErr;
}

OSStatus LocaleCountNames(LocaleRef ref, LocaleOperationVariant variant, uint32_t nameMask, unsigned long* countOut)
{
	return unimpErr;
}

OSStatus LocaleGetIndName(LocaleRef ref, LocaleOperationVariant variant, uint32_t nameMask, unsigned long index, unsigned long maxLen, unsigned long* lenOut, Utf16Char* displayName, LocaleRef* displayLocale)
{
	return unimpErr;
}

OSStatus LocaleGetRegionLanguageName(RegionCode regionCode, char name[256])
{
	return unimpErr;
}

OSStatus LocaleOperationGetName(LocaleOperationClass cls, LocaleRef ref, unsigned long maxLen, unsigned long* lenOut, Utf16Char* displayName)
{
	*lenOut = 0;
	*displayName = 0;
	return unimpErr;
}

OSStatus LocaleOperationCountNames(LocaleOperationClass cls, unsigned long* count)
{
	*count = 0;
	return unimpErr;
}

OSStatus LocaleOperationGetIndName(LocaleOperationClass cls, unsigned long index, unsigned long maxLen, unsigned long* lenOut, Utf16Char* displayName, LocaleRef* displayLocale)
{
	*displayName = 0;
	*displayLocale = 0;
	*lenOut = 0;
	return unimpErr;
}


