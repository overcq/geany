/*
 * Generated by ./misc/optlib2c from optlib/scss.ctags, Don't edit this manually.
 */
#include "general.h"
#include "parse.h"
#include "routines.h"
#include "field.h"
#include "xtag.h"


static void initializeSCSSParser (const langType language)
{

	addLanguageRegexTable (language, "toplevel");
	addLanguageRegexTable (language, "comment");
	addLanguageRegexTable (language, "interp");
	addLanguageRegexTable (language, "args");
	addLanguageRegexTable (language, "map");
	addLanguageRegexTable (language, "strs");
	addLanguageRegexTable (language, "strd");

	addLanguageTagMultiTableRegex (language, "toplevel",
	                               "^//[^\n]*\n?",
	                               "", "", "", NULL);
	addLanguageTagMultiTableRegex (language, "toplevel",
	                               "^/\\*",
	                               "", "", "{tenter=comment}", NULL);
	addLanguageTagMultiTableRegex (language, "toplevel",
	                               "^#\\{",
	                               "", "", "{tenter=interp}", NULL);
	addLanguageTagMultiTableRegex (language, "toplevel",
	                               "^'",
	                               "", "", "{tenter=strs}", NULL);
	addLanguageTagMultiTableRegex (language, "toplevel",
	                               "^\"",
	                               "", "", "{tenter=strd}", NULL);
	addLanguageTagMultiTableRegex (language, "toplevel",
	                               "^[ \t]([A-Za-z0-9_-]+)[ \t]*:[^\n]*\n?",
	                               "", "", "", NULL);
	addLanguageTagMultiTableRegex (language, "toplevel",
	                               "^@mixin[ \t]+([A-Za-z0-9_-]+)",
	                               "\\1", "m", "{tenter=args}", NULL);
	addLanguageTagMultiTableRegex (language, "toplevel",
	                               "^@function[ \t]+([A-Za-z0-9_-]+)",
	                               "\\1", "f", "{tenter=args}", NULL);
	addLanguageTagMultiTableRegex (language, "toplevel",
	                               "^@each[ \t]+\\$([A-Za-z0-9_-]+)[ \t]in[ \t]+",
	                               "\\1", "v", "", NULL);
	addLanguageTagMultiTableRegex (language, "toplevel",
	                               "^@for[ \t]+\\$([A-Za-z0-9_-]+)[ \t]from[ \t]+.*[ \t]+(to|through)[ \t]+[^{]+",
	                               "\\1", "v", "", NULL);
	addLanguageTagMultiTableRegex (language, "toplevel",
	                               "^@[^\n]+\n?",
	                               "", "", "", NULL);
	addLanguageTagMultiTableRegex (language, "toplevel",
	                               "^:[^{;]+;\n?",
	                               "", "", "", NULL);
	addLanguageTagMultiTableRegex (language, "toplevel",
	                               "^:[^\n;{]+\n",
	                               "", "", "", NULL);
	addLanguageTagMultiTableRegex (language, "toplevel",
	                               "^::?([A-Za-z0-9_-]+)[ \t]*[,({]",
	                               "", "", "", NULL);
	addLanguageTagMultiTableRegex (language, "toplevel",
	                               "^:[^\n{]+[;{]\n?",
	                               "", "", "", NULL);
	addLanguageTagMultiTableRegex (language, "toplevel",
	                               "^\\$([A-Za-z0-9_-]+)[ \t]*:[ \t]*\\(",
	                               "\\1", "v", "{tenter=map}", NULL);
	addLanguageTagMultiTableRegex (language, "toplevel",
	                               "^\\$([A-Za-z0-9_-]+)[ \t]*:[^\n]*\n?",
	                               "\\1", "v", "", NULL);
	addLanguageTagMultiTableRegex (language, "toplevel",
	                               "^[.]([A-Za-z0-9_-]+)",
	                               "\\1", "c", "", NULL);
	addLanguageTagMultiTableRegex (language, "toplevel",
	                               "^%([A-Za-z0-9_-]+)",
	                               "\\1", "P", "", NULL);
	addLanguageTagMultiTableRegex (language, "toplevel",
	                               "^#([A-Za-z0-9_-]+)",
	                               "\\1", "i", "", NULL);
	addLanguageTagMultiTableRegex (language, "toplevel",
	                               "^.",
	                               "", "", "", NULL);
	addLanguageTagMultiTableRegex (language, "comment",
	                               "^\\*/",
	                               "", "", "{tleave}", NULL);
	addLanguageTagMultiTableRegex (language, "comment",
	                               "^.",
	                               "", "", "", NULL);
	addLanguageTagMultiTableRegex (language, "interp",
	                               "^\\}",
	                               "", "", "{tleave}", NULL);
	addLanguageTagMultiTableRegex (language, "interp",
	                               "^.",
	                               "", "", "", NULL);
	addLanguageTagMultiTableRegex (language, "args",
	                               "^\\{",
	                               "", "", "{tleave}", NULL);
	addLanguageTagMultiTableRegex (language, "args",
	                               "^#\\{",
	                               "", "", "{tenter=interp}", NULL);
	addLanguageTagMultiTableRegex (language, "args",
	                               "^\\$([A-Za-z0-9_-]+)[ \t]*(:([ \t]*\\$)?|[,)])",
	                               "\\1", "z", "", NULL);
	addLanguageTagMultiTableRegex (language, "args",
	                               "^.",
	                               "", "", "", NULL);
	addLanguageTagMultiTableRegex (language, "map",
	                               "^//[^\n]*\n?",
	                               "", "", "", NULL);
	addLanguageTagMultiTableRegex (language, "map",
	                               "^/\\*",
	                               "", "", "{tenter=comment}", NULL);
	addLanguageTagMultiTableRegex (language, "map",
	                               "^#\\{",
	                               "", "", "{tenter=interp}", NULL);
	addLanguageTagMultiTableRegex (language, "map",
	                               "^\\)",
	                               "", "", "{tleave}", NULL);
	addLanguageTagMultiTableRegex (language, "map",
	                               "^([A-Za-z0-9_-]+)[ \t]*:",
	                               "\\1", "v", "", NULL);
	addLanguageTagMultiTableRegex (language, "map",
	                               "^'",
	                               "", "", "{tenter=strs}", NULL);
	addLanguageTagMultiTableRegex (language, "map",
	                               "^\"",
	                               "", "", "{tenter=strd}", NULL);
	addLanguageTagMultiTableRegex (language, "map",
	                               "^.",
	                               "", "", "", NULL);
	addLanguageTagMultiTableRegex (language, "strs",
	                               "^'",
	                               "", "", "{tleave}", NULL);
	addLanguageTagMultiTableRegex (language, "strs",
	                               "^#\\{",
	                               "", "", "{tenter=interp}", NULL);
	addLanguageTagMultiTableRegex (language, "strs",
	                               "^[^'#\\\\]+",
	                               "", "", "", NULL);
	addLanguageTagMultiTableRegex (language, "strs",
	                               "^\\\\?.",
	                               "", "", "", NULL);
	addLanguageTagMultiTableRegex (language, "strd",
	                               "^\"",
	                               "", "", "{tleave}", NULL);
	addLanguageTagMultiTableRegex (language, "strd",
	                               "^#\\{",
	                               "", "", "{tenter=interp}", NULL);
	addLanguageTagMultiTableRegex (language, "strd",
	                               "^[^\"#\\\\]+",
	                               "", "", "", NULL);
	addLanguageTagMultiTableRegex (language, "strd",
	                               "^\\\\?.",
	                               "", "", "", NULL);
}

extern parserDefinition* SCSSParser (void)
{
	static const char *const extensions [] = {
		"scss",
		NULL
	};

	static const char *const aliases [] = {
		NULL
	};

	static const char *const patterns [] = {
		NULL
	};

	static kindDefinition SCSSKindTable [] = {
		{
		  true, 'm', "mixin", "mixins",
		},
		{
		  true, 'f', "function", "functions",
		},
		{
		  true, 'v', "variable", "variables",
		},
		{
		  true, 'c', "class", "classes",
		},
		{
		  true, 'P', "placeholder", "placeholder classes",
		},
		{
		  true, 'i', "id", "identities",
		},
		{
		  true, 'z', "parameter", "function parameters",
		},
	};

	parserDefinition* const def = parserNew ("SCSS");

	def->versionCurrent= 0;
	def->versionAge    = 0;
	def->enabled       = true;
	def->extensions    = extensions;
	def->patterns      = patterns;
	def->aliases       = aliases;
	def->method        = METHOD_NOT_CRAFTED|METHOD_REGEX;
	def->kindTable     = SCSSKindTable;
	def->kindCount     = ARRAY_SIZE(SCSSKindTable);
	def->initialize    = initializeSCSSParser;

	return def;
}
