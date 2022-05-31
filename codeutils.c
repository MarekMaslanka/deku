/*
* Author: Marek Maślanka
* Project: KernelHotReload
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include <regex.h>

void *memmem(const void *haystack, size_t haystacklen, const void *needle, size_t needlelen);

static regex_t regexCtagsLine;

static void err(const char *format, ...) {
    va_list args;
    va_start(args, format);
    fprintf(stderr, format, args);
    va_end(args);
    fprintf(stderr, "\n");
	exit(EXIT_FAILURE);
}

static bool lineContains(char *text, const char *needle)
{
	char *eol = strchr(text, '\n');
	return memmem(text, eol - text, needle, strlen(needle));
}

static char *gotoLine(char *text, uint32_t line)
{
	line--;
	while (line > 0 && *text != '\0') {
		if (*text++ == '\n')
			line--;
	}
	return text;
}

static char *revstrchr(char *text, const char *stop, const char needle)
{
	if (text == stop)
		return NULL;
	while (text-- >= stop) {
		if (*text == needle)
			return text;
	}
	return text;
}

static char *gotoLineBegin(char *text, const char *stop)
{
	while (text-- >= stop) {
		if (*text == '\n')
			return text + 1;
	}
	return text;
}

static char *gotoPrevLine(char *text, const char *stop)
{
	if (text == stop)
		return NULL;
	text--;
	while (text > stop) {
		if (*--text == '\n')
			return text + 1;
	}
	return NULL;
}

static char *findEndOfFunc(char *text)
{
	int count = 0;
	while (*text != '\0') {
		if (*text == '{')
			count++;
		if (*text == '}') {
			count--;
			if (count == 0)
				return text;
		}
		text++;
	}
	return NULL;
}

static uint32_t findEndOfVariable(char *text, uint32_t *assignPos)
{
	const char *origin = text;
	*assignPos = 0;
	while (*text != '\0') {
		if (*text == '=' && *assignPos == 0) {
			*assignPos = text - origin;
		}
		if (*text == ';') {
			return text - origin;
		}
		text++;
	}
	return 0;
}

// "text" must point to end brace of struct
static char *findBeginOfStruct(char *text, const char *stop, uint32_t *lineno)
{
	char *line;
	uint32_t cnt = 1;
	if (*text != '}') {
		fprintf(stderr, "Pointer does not point to end of struct brace");
		return NULL;
	}
	text--;
	while (text >= stop) {
		if (*text == '{') {
			cnt--;
			if (cnt == 0)
				goto findStruct;
		}
		if (*text == '}')
			cnt++;
		if (*text == '\n')
			(*lineno)--;
		text--;
	}
	return NULL;
findStruct:
	line = gotoLineBegin(text, stop);
	if (lineContains(line, "struct"))
		return line;
	(*lineno)--;
	text = gotoPrevLine(line, stop);
	return text;
}

static void removeComments(char *text) {
	bool isSingleLineComment = false;
	bool isMultiLineLineComment = false;
	while (*text != '\0') {
		if (*text == '\n')
			isSingleLineComment = false;
		else if (!isMultiLineLineComment && *text == '/' && *(text + 1) == '/') {
			isSingleLineComment = true;
			*text = ' ';
		} else if (!isSingleLineComment) {
			if (*text == '/' && *(text + 1) == '*') {
				isMultiLineLineComment = true;
				*text = ' ';
			}
			if (*text == '*' && *(text + 1) == '/') {
				isMultiLineLineComment = false;
				*text++ = ' ';
				*text = ' ';
			}
		}
		if (isSingleLineComment || isMultiLineLineComment)
			if (*text != '\n' && *text != '\r' && *text != '\t')
				*text = ' ';
		text++;
	}
}

static bool writeToFile(const char *path, const char *text) {
	FILE *fp = fopen(path, "w");
	fwrite(text, strlen(text), 1, fp);
	fclose(fp);
	return true;
}

static char *readFile(const char *path) {
	FILE *fp = fopen(path, "r");
	fseek(fp, 0L, SEEK_END);
	long size = ftell(fp);
	rewind(fp);
	char *buf = (char *)malloc(size + 1 + 1 /* to avoid additional check when remove comments */);
	fread(buf, size, 1, fp);
	buf[size] = '\0';
	fclose(fp);
	return buf;
}

static void extendsCtags(const char *ctagsPath, const char *srcPath) {
	FILE *fp;
	FILE *efp;
	char *line = NULL;
	size_t len = 0;
	size_t read;
	uint32_t start;
	uint32_t end;

	char *srcFile = readFile(srcPath);
	char *text = srcFile;

	removeComments(text);
	// writeToFile("out.c", text);

	fp = fopen(ctagsPath, "r");
	if (fp == NULL)
		err("Failed to open ctags file");
	efp = fopen("enhanced_ctags", "w");
	if (efp == NULL)
		err("Failed to open ctags file to write");

	regmatch_t groupArray[4];

	if (regcomp(&regexCtagsLine, "^([a-zA-z0-9_:]+)\\s+(\\w+)\\s+([0-9]+)\\s+.+", REG_EXTENDED))
		err("Could not compile regular expression.");

	while ((read = getline(&line, &len, fp)) != -1) {
		if (regexec(&regexCtagsLine, line, sizeof(groupArray)/sizeof(groupArray[0]), groupArray, 0) == 0) {
			char tagLineStr[16] = {0};
			uint32_t tagLineNo = 0;
			const char *s = line + groupArray[3].rm_so;
			const char *e = line + groupArray[3].rm_eo;
			memcpy(tagLineStr, s, e - s);
			tagLineNo = atoi(tagLineStr);
			s = line + groupArray[2].rm_so;
			e = line + groupArray[2].rm_eo;
			fwrite(line, groupArray[2].rm_eo, 1, efp);
			if (memcmp("variable", s, e - s) == 0) {
				uint32_t assign;
				text = gotoLine(srcFile, tagLineNo);
				// check whether variable type is struct declaration
				if (strstr(line, ".c }")) {
					char *s = strchr(line, '}');
					if (s != NULL) {
						uint32_t no = tagLineNo;
						s = findBeginOfStruct(s, srcFile, &no);
						if (s != NULL) {
							text = s;
							tagLineNo = no;
						} else {
							printf("Failed to find begin of struct for variable in line: %d\n", tagLineNo);
						}
					}
				}
				// check whether prev line is part of the variable
				char *prev = gotoPrevLine(text, srcFile);
				if (prev && *prev != '\n' && *prev != ' ' && *prev != '#' &&
					!lineContains(prev, ";")  && !lineContains(prev, "}")) {
					text = prev;
					tagLineNo--;
				}
				start = text - srcFile;
				end = findEndOfVariable(text, &assign);
				end += text - srcFile;
				assign += (assign != 0) ? start : 0;
				fprintf(efp, "    %d", tagLineNo);
				fprintf(efp, " %d:%d:%d", start, assign, end);
			} else if (memcmp("prototype", s, e - s) == 0) {
				uint32_t assign;
				text = gotoLine(srcFile, tagLineNo);
				start = text - srcFile;
				end = findEndOfVariable(text, &assign);
				end += text - srcFile;
				fprintf(efp, "    %d", tagLineNo);
				fprintf(efp, " %d:%d", start, end);
			} else if (memcmp("function", s, e - s) == 0) {
				text = gotoLine(srcFile, tagLineNo);
				char *funNameBegin = NULL;
				// check if return type is struct declaration
				if(strstr(line, ".c }") && *text == '}') {
					uint32_t no = tagLineNo;
					char *s = findBeginOfStruct(text, srcFile, &no);
					if (s != NULL) {
						funNameBegin = text + 1;
						text = s;
						tagLineNo = no;
					}
				}
				// check whether prev line is part of the function
				char *prev = gotoPrevLine(text, srcFile);
				while (prev && prev != srcFile) {
					if (memcmp(prev, "__releases(", strlen("__releases(")) != 0 &&
						memcmp(prev, "__acquires(", strlen("__acquires(")) != 0)
						break;
					prev = gotoPrevLine(prev, srcFile);
					tagLineNo--;
				}
				if (prev && *prev != '\n' && *prev != ' ' && *prev != '#' && *prev != '}' && !lineContains(prev, ";")) {
					text = prev;
					tagLineNo--;
				}
				start = text - srcFile;
				text = findEndOfFunc(funNameBegin != NULL ? funNameBegin : text);
				if (text) {
					end = text - srcFile + 1;
				} else {
					// something goes wrong. Use next symbol's position to guess end of current function
					char *nextLine = NULL;
					regmatch_t groupArray[4];
					int originPos = ftell(efp);
					if (getline(&nextLine, &len, fp) == 0)
						err("Can not find end of function");
					if (regexec(&regexCtagsLine, nextLine, sizeof(groupArray)/sizeof(groupArray[0]), groupArray, 0) != 0)
						err("Can not find end of function based on next tag");
					char tagLineStr[16] = {0};
					uint32_t tagLineNo = 0;
					const char *s = nextLine + groupArray[3].rm_so;
					const char *e = nextLine + groupArray[3].rm_eo;
					memcpy(tagLineStr, s, e - s);
					tagLineNo = atoi(tagLineStr);
					text = gotoLine(srcFile, tagLineNo);
					text = revstrchr(text, srcFile, '}');
					end = text - srcFile + 1;
					fseek(efp, originPos, SEEK_SET);
				}
				fprintf(efp, "    %d", tagLineNo);
				fprintf(efp, " %d:%d", start, end);
			} else {
				fprintf(efp, "    %d", tagLineNo);
				fwrite(" :", 2, 1, efp);
			}
			fwrite(line + groupArray[3].rm_eo, read - groupArray[3].rm_eo, 1, efp);
		}
	}

	regfree(&regexCtagsLine);
	fclose(fp);
	fclose(efp);
	free(srcFile);
	if (line)
		free(line);
}

static void blanksFile(const char *path, const char *pattern) {
	uint32_t start, end;
	int res = sscanf(pattern, "%d:%d", &start, &end);
	if(res != 2 || start < 0 || end < 0)
		err("Invalid pattern to blanks the file");
	char *text = readFile(path);
	while(start <= end) {
		if(text[start] != '\n' && text[start] != '\r' && text[start] != '\t')
		   text[start] = ' ';
		start++;
	}
	writeToFile(path, text);
}

static void help()
{
	fprintf(stderr, "Invalid usage\n");
	puts("ctags <ctags file> <source file> - Extend the ctags file with the exact positions of the symbols in the source file");
	puts("blanks <start>:<end> - Replace the contents in the specified range with a space character");
}

int main(int argc, char *argv[])
{
	if(strcmp(argv[1], "ctags") == 0 && argc == 4) {
		extendsCtags(argv[2], argv[3]);
		return 0;
	}
	else if(strcmp(argv[1], "blanks") == 0 && argc == 4) {
		blanksFile(argv[2], argv[3]);
		return 0;
	} else {
		help();
	}

	return 1;
}