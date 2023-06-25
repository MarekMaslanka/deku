/*
* Author: Marek Maślanka
* Project: DEKU
* URL: https://github.com/MarekMaslanka/deku
*/

#include <vector>
#include <clang-c/Index.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define DEKU_INSPECT_FUNC "__DEKU_inspect_fun"
#define DEKU_INSPECT_VAR "__DEKU_inspect"
#define DEKU_INSPECT_RETURN "__DEKU_inspect_return"
#define DEKU_INSPECT_RETURN_VALUE "__DEKU_inspect_return_value"
#define DEKU_INSPECT_FUN_END "__DEKU_inspect_fun_end"
#define DEKU_INSPECT_FUN_POINTER "__DEKU_inspect_fun_pointer"
#define DEKU_INSPECT_STACK_TRACE "__deku_gen_stacktrace"

#define LOG_DEBUG(fmt, ...)
#define LOG_INFO(fmt, ...) printf(fmt, ##__VA_ARGS__)
#define CHECK_ALLOC(ptr) \
	if (ptr == NULL) {   \
		LOG_INFO("Failed to alloc memory in %s (%s:%d)", __func__, __FILE__, __LINE__); \
		exit(1);\
	}
typedef struct
{
	CXSourceRange range;
	CXCursor cursor;
} InspectIfCond;
typedef struct
{
	std::vector<InspectIfCond> conds;
	CXSourceLocation parenEnd;
	CXSourceLocation stmtEnd;
} InspectIfConds;

typedef struct
{
	CXSourceRange range;
	char *varName;
	bool init;
	CXCursor cursor;
} InspectVar;

typedef struct
{
	CXSourceRange range;
	char *ptr;
	char *params;
	bool varAssign;
	InspectVar var;
} InspectFunPointer;

typedef struct
{
	CXSourceRange range;
	bool valueNonLiteral;
	char *funName;
} InspectReturn;

typedef struct
{
	CXSourceRange range;
	CXSourceLocation openCurlyParen;
	char *name;
} InspectFunction;

typedef enum
{
	INSPECT_VAR,
	INSPECT_IF_COND,
	INSPECT_FUN_PTR,
	INSPECT_RETURN,
	INSPECT_RETURN_VALUE,
	INSPECT_FUNCTION,
	INSPECT_FUNCTION_END,
} InspectType;
typedef struct
{
	InspectType type;
	InspectIfConds ifStmtConds;
	InspectVar var;
	InspectFunPointer funPtr;
	InspectReturn ret;
	InspectFunction func;
} Inspect;

typedef struct
{
	char *name;
	unsigned line;
	std::vector<Inspect> inspects;
} FunctionInspectCtx;

CXTranslationUnit translationUnit;

FILE *inspectMapFile;

static const unsigned int crc32Table[] =
{
  0x00000000, 0x04c11db7, 0x09823b6e, 0x0d4326d9,
  0x130476dc, 0x17c56b6b, 0x1a864db2, 0x1e475005,
  0x2608edb8, 0x22c9f00f, 0x2f8ad6d6, 0x2b4bcb61,
  0x350c9b64, 0x31cd86d3, 0x3c8ea00a, 0x384fbdbd,
  0x4c11db70, 0x48d0c6c7, 0x4593e01e, 0x4152fda9,
  0x5f15adac, 0x5bd4b01b, 0x569796c2, 0x52568b75,
  0x6a1936c8, 0x6ed82b7f, 0x639b0da6, 0x675a1011,
  0x791d4014, 0x7ddc5da3, 0x709f7b7a, 0x745e66cd,
  0x9823b6e0, 0x9ce2ab57, 0x91a18d8e, 0x95609039,
  0x8b27c03c, 0x8fe6dd8b, 0x82a5fb52, 0x8664e6e5,
  0xbe2b5b58, 0xbaea46ef, 0xb7a96036, 0xb3687d81,
  0xad2f2d84, 0xa9ee3033, 0xa4ad16ea, 0xa06c0b5d,
  0xd4326d90, 0xd0f37027, 0xddb056fe, 0xd9714b49,
  0xc7361b4c, 0xc3f706fb, 0xceb42022, 0xca753d95,
  0xf23a8028, 0xf6fb9d9f, 0xfbb8bb46, 0xff79a6f1,
  0xe13ef6f4, 0xe5ffeb43, 0xe8bccd9a, 0xec7dd02d,
  0x34867077, 0x30476dc0, 0x3d044b19, 0x39c556ae,
  0x278206ab, 0x23431b1c, 0x2e003dc5, 0x2ac12072,
  0x128e9dcf, 0x164f8078, 0x1b0ca6a1, 0x1fcdbb16,
  0x018aeb13, 0x054bf6a4, 0x0808d07d, 0x0cc9cdca,
  0x7897ab07, 0x7c56b6b0, 0x71159069, 0x75d48dde,
  0x6b93dddb, 0x6f52c06c, 0x6211e6b5, 0x66d0fb02,
  0x5e9f46bf, 0x5a5e5b08, 0x571d7dd1, 0x53dc6066,
  0x4d9b3063, 0x495a2dd4, 0x44190b0d, 0x40d816ba,
  0xaca5c697, 0xa864db20, 0xa527fdf9, 0xa1e6e04e,
  0xbfa1b04b, 0xbb60adfc, 0xb6238b25, 0xb2e29692,
  0x8aad2b2f, 0x8e6c3698, 0x832f1041, 0x87ee0df6,
  0x99a95df3, 0x9d684044, 0x902b669d, 0x94ea7b2a,
  0xe0b41de7, 0xe4750050, 0xe9362689, 0xedf73b3e,
  0xf3b06b3b, 0xf771768c, 0xfa325055, 0xfef34de2,
  0xc6bcf05f, 0xc27dede8, 0xcf3ecb31, 0xcbffd686,
  0xd5b88683, 0xd1799b34, 0xdc3abded, 0xd8fba05a,
  0x690ce0ee, 0x6dcdfd59, 0x608edb80, 0x644fc637,
  0x7a089632, 0x7ec98b85, 0x738aad5c, 0x774bb0eb,
  0x4f040d56, 0x4bc510e1, 0x46863638, 0x42472b8f,
  0x5c007b8a, 0x58c1663d, 0x558240e4, 0x51435d53,
  0x251d3b9e, 0x21dc2629, 0x2c9f00f0, 0x285e1d47,
  0x36194d42, 0x32d850f5, 0x3f9b762c, 0x3b5a6b9b,
  0x0315d626, 0x07d4cb91, 0x0a97ed48, 0x0e56f0ff,
  0x1011a0fa, 0x14d0bd4d, 0x19939b94, 0x1d528623,
  0xf12f560e, 0xf5ee4bb9, 0xf8ad6d60, 0xfc6c70d7,
  0xe22b20d2, 0xe6ea3d65, 0xeba91bbc, 0xef68060b,
  0xd727bbb6, 0xd3e6a601, 0xdea580d8, 0xda649d6f,
  0xc423cd6a, 0xc0e2d0dd, 0xcda1f604, 0xc960ebb3,
  0xbd3e8d7e, 0xb9ff90c9, 0xb4bcb610, 0xb07daba7,
  0xae3afba2, 0xaafbe615, 0xa7b8c0cc, 0xa379dd7b,
  0x9b3660c6, 0x9ff77d71, 0x92b45ba8, 0x9675461f,
  0x8832161a, 0x8cf30bad, 0x81b02d74, 0x857130c3,
  0x5d8a9099, 0x594b8d2e, 0x5408abf7, 0x50c9b640,
  0x4e8ee645, 0x4a4ffbf2, 0x470cdd2b, 0x43cdc09c,
  0x7b827d21, 0x7f436096, 0x7200464f, 0x76c15bf8,
  0x68860bfd, 0x6c47164a, 0x61043093, 0x65c52d24,
  0x119b4be9, 0x155a565e, 0x18197087, 0x1cd86d30,
  0x029f3d35, 0x065e2082, 0x0b1d065b, 0x0fdc1bec,
  0x3793a651, 0x3352bbe6, 0x3e119d3f, 0x3ad08088,
  0x2497d08d, 0x2056cd3a, 0x2d15ebe3, 0x29d4f654,
  0xc5a92679, 0xc1683bce, 0xcc2b1d17, 0xc8ea00a0,
  0xd6ad50a5, 0xd26c4d12, 0xdf2f6bcb, 0xdbee767c,
  0xe3a1cbc1, 0xe760d676, 0xea23f0af, 0xeee2ed18,
  0xf0a5bd1d, 0xf464a0aa, 0xf9278673, 0xfde69bc4,
  0x89b8fd09, 0x8d79e0be, 0x803ac667, 0x84fbdbd0,
  0x9abc8bd5, 0x9e7d9662, 0x933eb0bb, 0x97ffad0c,
  0xafb010b1, 0xab710d06, 0xa6322bdf, 0xa2f33668,
  0xbcb4666d, 0xb8757bda, 0xb5365d03, 0xb1f740b4
};

static uint32_t crc32(uint8_t *data, uint32_t len)
{
	unsigned int crc = 0;
	while (len--)
	{
		crc = (crc << 8) ^ crc32Table[((crc >> 24) ^ *data) & 255];
		data++;
	}
	return crc;
}

char *getOriginSource(CXSourceLocation start, CXSourceLocation end)
{
	CXFile file;
	unsigned int sOffset, eOffset;
	clang_getFileLocation(start, &file, NULL, NULL, &sOffset);
	clang_getFileLocation(end, NULL, NULL, NULL, &eOffset);
	const char *buf = clang_getFileContents(translationUnit, file, NULL);
	char *result = (char *)calloc(eOffset - sOffset + 1, 1);
	CHECK_ALLOC(result);
	memcpy(result, &buf[sOffset], eOffset - sOffset);
	return result;
}

Inspect *containsIfCondition(std::vector<Inspect> *list, CXSourceRange range, unsigned *outRangeIndex)
{
	for (auto &inspect : *list)
	{
		if (inspect.type != INSPECT_IF_COND)
			continue;
		for (int i = 0; i < inspect.ifStmtConds.conds.size(); i++)
		{
			if (clang_equalRanges(range, inspect.ifStmtConds.conds[i].range))
			{
				*outRangeIndex = i;
				return &inspect;
			}
		}
	}
	return NULL;
}

bool isFunctionCallPtr(CXCursor cursor, CXSourceLocation *openParen)
{
	CXToken *tokens;
	unsigned numTokens;
	clang_tokenize(translationUnit, clang_getCursorExtent(cursor), &tokens, &numTokens);
	bool isPointer = false;
	bool result = false;
	for (int i = 0; i < numTokens; i++)
	{
		CXString cStr = clang_getTokenSpelling(translationUnit, tokens[i]);
		const char *str = clang_getCString(cStr);
		if (strcmp(str, ".") == 0 || strcmp(str, "->") == 0)
			isPointer = true;
		else if (strcmp(str, "(") == 0)
		{
			if (isPointer)
			{
				result = true;
				if (openParen)
					*openParen = clang_getTokenLocation(translationUnit, tokens[i]);
			}
			clang_disposeString(cStr);
			break;
		}
		clang_disposeString(cStr);
	}
	return result;
}

bool getCallFunctionPtr(CXCursor cursor, char **ptr, char **params)
{
	CXSourceLocation openParen;
	if (isFunctionCallPtr(cursor, &openParen))
	{
		CXSourceRange range = clang_getCursorExtent(cursor);
		*ptr = getOriginSource(clang_getRangeStart(range), openParen);
		char *temp = getOriginSource(openParen, clang_getRangeEnd(range));
		temp[strlen(temp) - 1] = '\0';
		*params = strdup(temp + 1);
		CHECK_ALLOC(params);
		free(temp);
		return true;
	}
	return false;
}

void addIfTestResult(InspectIfConds *ctx)
{
	unsigned int endParenLine, endParenColumn, endStmtLine, endStmtColumn;
	clang_getPresumedLocation(ctx->parenEnd, NULL, &endParenLine, &endParenColumn);
	clang_getPresumedLocation(ctx->stmtEnd, NULL, &endStmtLine, &endStmtColumn);
	FILE *f = fopen("test.txt", "a+");
	fprintf(f, "<%d:%d, %d:%d>\n", endParenLine, endParenColumn, endStmtLine, endStmtColumn);
	for (InspectIfCond &cond : ctx->conds)
	{
		unsigned int sLine, sColumn, eLine, eColumn, sOffset;
		clang_getPresumedLocation(clang_getRangeStart(cond.range), NULL, &sLine, &sColumn);
		clang_getPresumedLocation(clang_getRangeEnd(cond.range), NULL, &eLine, &eColumn);
		fprintf(f, "\t<%d:%d, %d:%d>\n", sLine, sColumn, eLine, eColumn);
	}
	fclose(f);
}

void addVarTestResult(InspectVar *var)
{
	unsigned int sLine, sColumn, eLine, eColumn;
	clang_getPresumedLocation(clang_getRangeStart(var->range), NULL, &sLine, &sColumn);
	clang_getPresumedLocation(clang_getRangeEnd(var->range), NULL, &eLine, &eColumn);
	FILE *f = fopen("test.txt", "a+");
	fprintf(f, "<%d:%d, %d:%d> %s\n", sLine, sColumn, eLine, eColumn, var->varName);
	fclose(f);
}

bool canBeInspected(CXCursor cursor)
{
	CXTypeKind type = clang_getCursorType(cursor).kind;
	CXTypeKind canonType = clang_getCanonicalType(clang_getCursorType(cursor)).kind;

	if (type == CXType_Elaborated || (type == CXType_Typedef && canonType == CXType_Record))
		return false;
	return true;
}

void addIfStmtCond(CXCursor cursor, InspectIfConds *ctx)
{
	CXSourceRange range = clang_getCursorExtent(cursor);
	unsigned int sLine, sColumn, eLine, eColumn, endParenLine, endParenColumn;
	clang_getPresumedLocation(clang_getRangeEnd(range), NULL, &eLine, &eColumn);
	clang_getPresumedLocation(ctx->parenEnd, NULL, &endParenLine, &endParenColumn);
	if (endParenLine > eLine || (endParenLine == eLine && endParenColumn > eColumn))
	{
		bool exists = false;
		clang_getPresumedLocation(clang_getRangeStart(range), NULL, &sLine, &sColumn);
		for (InspectIfCond &cond : ctx->conds)
		{
			unsigned int sLine2, sColumn2, eLine2, eColumn2;
			clang_getPresumedLocation(clang_getRangeStart(cond.range), NULL, &sLine2, &sColumn2);
			clang_getPresumedLocation(clang_getRangeEnd(cond.range), NULL, &eLine2, &eColumn2);
			// clang_equalRanges(r, range); does't work for unknown reason
			if ((sLine2 == sLine && sColumn2 == sColumn) || (eLine2 == eLine && eColumn2 == eColumn))
			{
				exists = true;
				break;
			}
		}
		if (!exists)
			ctx->conds.push_back({ .range = range, .cursor = cursor });
	}
}

CXChildVisitResult ifStmtCondVisitor(CXCursor cursor, CXCursor parent, CXClientData ctx)
{
	CXCursorKind kind = clang_getCursorKind(cursor);

	if (kind == CXCursor_BinaryOperator || kind == CXCursor_ParenExpr)
		clang_visitChildren(cursor, *ifStmtCondVisitor, ctx);
	if (kind == CXCursor_FirstExpr)
		addIfStmtCond(cursor, (InspectIfConds *)ctx);
	if (kind == CXCursor_CallExpr)
	{
		addIfStmtCond(cursor, (InspectIfConds *)ctx);
		return CXChildVisit_Continue;
	}
	// if (kind == CXCursor_IntegerLiteral)
	// {
	// 	CXToken *token = clang_getToken(translationUnit, clang_getCursorLocation(cursor));
	// 	puts(clang_getCString(clang_getTokenSpelling(translationUnit, *token)));
	// 	return CXChildVisit_Continue;
	// }

	return CXChildVisit_Recurse;
}

CXChildVisitResult funBodyInspectVisitor(CXCursor cursor, CXCursor parent, CXClientData data)
{
	unsigned int sLine, sColumn, eLine, eColumn, sOffset, eOffset;
	CXCursorKind kind = clang_getCursorKind(cursor);
	FunctionInspectCtx *ctx = (FunctionInspectCtx *)data;
	CXSourceRange range;
	unsigned numTokens;
	CXToken *tokens;
	const char *buf;
	CXFile file;
	if (kind == CXCursor_IfStmt)
	{
		range = clang_getCursorExtent(cursor);
		InspectIfConds ifConds;
		ifConds.stmtEnd = clang_getRangeEnd(range);

		unsigned numTokens;
		clang_tokenize(translationUnit, range, &tokens, &numTokens);
		unsigned paren = 0;
		for (int i = 0; i < numTokens; i++)
		{
			CXString cStr = clang_getTokenSpelling(translationUnit, tokens[i]);
			const char *str = clang_getCString(cStr);
			if (strcmp(str, "(") == 0)
				paren++;
			if (strcmp(str, ")") == 0)
			{
				paren--;
				if (paren == 0)
				{
					range = clang_getTokenExtent(translationUnit, tokens[i]);
					ifConds.parenEnd = clang_getRangeEnd(range);
					clang_disposeString(cStr);
					break;
				}
			}
			clang_disposeString(cStr);
		}
		clang_disposeTokens(translationUnit, tokens, numTokens);

		clang_visitChildren(cursor, *ifStmtCondVisitor, &ifConds);
		ctx->inspects.push_back({.type = INSPECT_IF_COND, .ifStmtConds = ifConds});
		addIfTestResult(&ifConds);
	}
	else if (kind == CXCursor_VarDecl)
	{
		if (clang_Cursor_isNull(clang_Cursor_getVarDeclInitializer(cursor)))
			return CXChildVisit_Continue;
		if (clang_getCursorKind(parent) == CXCursor_ForStmt)
			return CXChildVisit_Recurse;
		if (!canBeInspected(cursor))
			return CXChildVisit_Recurse;

		range = clang_getCursorExtent(cursor);
		clang_getSpellingLocation(clang_getRangeStart(range), &file, &sLine, &sColumn, &sOffset);
		clang_getSpellingLocation(clang_getRangeEnd(range), NULL, &eLine, &eColumn, &eOffset);
		buf = clang_getFileContents(translationUnit, file, NULL);
		if (memchr(buf + sOffset, '=', eOffset - sOffset))
		{
			clang_Type_getSizeOf(clang_getCursorType(cursor));
			CXString name = clang_getCursorSpelling(cursor);
			InspectVar var = {};
			var.range = range;
			var.init = true;
			var.cursor = cursor;
			var.varName = strdup(clang_getCString(name));
			CHECK_ALLOC(var.varName);
			ctx->inspects.push_back({.type = INSPECT_VAR, .var = var});
			LOG_DEBUG("%s (<%i:%i, %i:%i>)\n", clang_getCString(name), sLine, sColumn, eLine, eColumn);
			clang_disposeString(name);
			addVarTestResult(&var);
		}
	}
	else if (kind == CXCursor_BinaryOperator || kind == CXCursor_CompoundAssignOperator)
	{
		if (clang_getCursorKind(parent) == CXCursor_ForStmt)
			return CXChildVisit_Recurse;

		range = clang_getCursorExtent(cursor);
		clang_tokenize(translationUnit, range, &tokens, &numTokens);
		if (numTokens == 0)
		{
			clang_getSpellingLocation(clang_getRangeStart(range), &file, &sLine, &sColumn, NULL);
			clang_getSpellingLocation(clang_getRangeEnd(range), NULL, &eLine, &eColumn, NULL);
			LOG_INFO("Fail to fetch tokens for: <%d:%d, %d:%d>\n", sLine, sColumn, eLine, eColumn);
			clang_disposeTokens(translationUnit, tokens, numTokens);
			return CXChildVisit_Recurse;
		}

		CXString cStr = clang_getTokenSpelling(translationUnit, tokens[1]);
		const char *str = clang_getCString(cStr);
		if (strcmp(str, "=") == 0 || kind == CXCursor_CompoundAssignOperator)
		{
			clang_getSpellingLocation(clang_getRangeEnd(range), NULL, &eLine, &eColumn, NULL);

			unsigned varLine, varStartColumn, varEndColumn;
			CXSourceRange tokenRange = clang_getTokenExtent(translationUnit, tokens[0]);
			clang_getSpellingLocation(clang_getRangeStart(tokenRange), NULL, &varLine, &varStartColumn, NULL);
			clang_getSpellingLocation(clang_getRangeEnd(tokenRange), NULL, NULL, &varEndColumn, NULL);

			if (!canBeInspected(cursor))
				return CXChildVisit_Recurse;

			CXString name = clang_getTokenSpelling(translationUnit, tokens[0]);
			InspectVar var = {};
			var.range = range;
			var.init = false;
			var.cursor = cursor;
			var.varName = strdup(clang_getCString(name));
			CHECK_ALLOC(var.varName);
			ctx->inspects.push_back({.type = INSPECT_VAR, .var = var});

			LOG_DEBUG("<%d %d:%d, %d:%d>\n", varLine, varStartColumn, varEndColumn, eLine, eColumn);
			addVarTestResult(&var);
			clang_disposeString(name);
		}
		clang_disposeString(cStr);
		clang_disposeTokens(translationUnit, tokens, numTokens);
	}
	else if (kind == CXCursor_ReturnStmt)
	{
		range = clang_getCursorExtent(cursor);
		clang_tokenize(translationUnit, range, &tokens, &numTokens);
		InspectReturn ret = {};
		ret.range = range;
		ret.funName = strdup(ctx->name);
		ret.valueNonLiteral = false;
		if (numTokens == 2)
			ret.valueNonLiteral = clang_getTokenKind(tokens[1]) != CXToken_Literal;
		else if (numTokens > 2)
			ret.valueNonLiteral = true;
		ctx->inspects.push_back({.type = numTokens > 1 ? INSPECT_RETURN_VALUE : INSPECT_RETURN, .ret = ret});
		clang_disposeTokens(translationUnit, tokens, numTokens);
	}
	else if (kind == CXCursor_CallExpr)
	{
		range = clang_getCursorExtent(cursor);
		if (isFunctionCallPtr(cursor, NULL))
		{
			InspectFunPointer funPtr = {};
			auto prev = ctx->inspects.back();
			if (prev.type == INSPECT_VAR &&
				clang_equalLocations(clang_getRangeEnd(prev.var.range), clang_getRangeEnd(range)))
			{
				funPtr.varAssign = true;
				funPtr.var = prev.var;
				ctx->inspects.pop_back();
			}
			else
			{
				unsigned index;
				Inspect *parentInspect = containsIfCondition(&ctx->inspects, range, &index);
				if (parentInspect)
					return CXChildVisit_Continue;
			}
			getCallFunctionPtr(cursor, &funPtr.ptr, &funPtr.params);
			funPtr.range = range;
			ctx->inspects.push_back({.type = INSPECT_FUN_PTR, .funPtr = funPtr});
		}
	}
	else if (kind == CXCursor_DeclStmt)
	{
		if (clang_getCursorKind(parent) == CXCursor_ForStmt)
			return CXChildVisit_Continue;
	}

	return CXChildVisit_Recurse;
}

CXChildVisitResult functionInspectVisitor(CXCursor cursor, CXCursor parent, CXClientData data)
{
	if (!clang_Location_isFromMainFile(clang_getCursorLocation(cursor)))
		return CXChildVisit_Continue;

	FunctionInspectCtx *funCtx = (FunctionInspectCtx *)data;
	CXCursorKind kind = clang_getCursorKind(cursor);
	CXString name = clang_getCursorSpelling(cursor);
	if (kind == CXCursor_FunctionDecl)
	{
		if (strcmp(clang_getCString(name), funCtx->name) == 0)
		{
			CXSourceRange range = clang_getCursorExtent(cursor);
			InspectFunction func = {};
			func.range = range;
			func.name = strdup(funCtx->name);
			func.openCurlyParen = clang_getNullLocation();

			CXToken *tokens;
			unsigned numTokens;
			clang_tokenize(translationUnit, range, &tokens, &numTokens);
			for (int i = 0; i < numTokens; i++)
			{
				CXString text = clang_getTokenSpelling(translationUnit, tokens[i]);
				if (strcmp(clang_getCString(text), "{") == 0 &&
					clang_equalLocations(func.openCurlyParen, clang_getNullLocation()))
				{
					func.openCurlyParen = clang_getTokenLocation(translationUnit, tokens[i]);
					clang_disposeString(text);
					break;
				}
				clang_disposeString(text);
			}
			clang_disposeTokens(translationUnit, tokens, numTokens);

			funCtx->inspects.push_back({.type = INSPECT_FUNCTION, .func = func});
			clang_visitChildren(cursor, *funBodyInspectVisitor, funCtx);
			func = {};
			func.range = range;
			func.name = strdup(funCtx->name);
			funCtx->inspects.push_back({.type = INSPECT_FUNCTION_END, .func = func});

			clang_getFileLocation(clang_getRangeStart(range), NULL, &funCtx->line, NULL, NULL);
			clang_disposeString(name);
			return CXChildVisit_Break;
		}
		clang_disposeString(name);
		return CXChildVisit_Continue;
	}

	clang_disposeString(name);
	return CXChildVisit_Recurse;
}

void getInspectionsForFunction(FunctionInspectCtx *funCtx)
{
	CXCursor rootCursor = clang_getTranslationUnitCursor(translationUnit);
	unsigned int res = clang_visitChildren(rootCursor, *functionInspectVisitor, funCtx);
}

uint32_t genId(CXSourceRange range, const char *filePath, const char *text, const char *extra, InspectType type)
{
	unsigned int line, column, offset;
	CXSourceLocation startloc = type != INSPECT_FUNCTION_END ?
										clang_getRangeStart(range) :
										clang_getRangeEnd(range);
	clang_getFileLocation(startloc, NULL, &line, &column, &offset);
	CXToken *token = clang_getToken(translationUnit, startloc);
	if (token == NULL) {
		LOG_INFO("Invalid token at %d:%d\n", line, column);
		return 0;
	}
	size_t bufSize = strlen(filePath) + strlen(text) + 32;
	char *buf = (char *)malloc(bufSize);
	snprintf(buf, bufSize, "%s:%d:%s:%s:%d", filePath, line, text, extra, type);
	uint32_t sum = crc32((uint8_t *)buf, strlen(buf));
	fprintf(inspectMapFile, "%s:%u\n", buf, sum);
	free(buf);
	return sum;
}

void applyInspections(std::vector<Inspect> *inspects, char *filePath)
{
	CXFile cxFile;
	size_t fSize;
	clang_getFileLocation(clang_getRangeEnd(inspects->at(0).func.range), &cxFile, NULL, NULL, NULL);
	const char *sourceBuf = clang_getFileContents(translationUnit, cxFile, &fSize);
	CXString srcFilePath = clang_getFileName(cxFile);
	FILE *outFile = fopen(clang_getCString(srcFilePath), "w");
	CXSourceRange range;
	unsigned prevPos = 0;
	unsigned offset, sLine, eLine;
	uint32_t id;
	for (Inspect &inspect : *inspects)
	{
		if (inspect.type == INSPECT_VAR)
		{
			range = inspect.var.range;
			clang_getFileLocation(clang_getRangeStart(range), NULL, NULL, NULL, &offset);
			fwrite(&sourceBuf[prevPos], 1, offset - prevPos, outFile);
			prevPos = offset;
			if (!inspect.var.init)
				fwrite("{", 1, 1, outFile);
			prevPos = offset;
			clang_getFileLocation(clang_getRangeEnd(range), NULL, NULL, NULL, &offset);
			fwrite(&sourceBuf[prevPos], 1, offset - prevPos + 1, outFile);
			prevPos = offset + 1;
			id = genId(range, filePath, inspect.var.varName, "", inspect.type);
			fprintf(outFile, "%s(%u, \"%s\", %s);", DEKU_INSPECT_VAR, id, filePath, inspect.var.varName);
			if (!inspect.var.init)
				fwrite("}", 1, 1, outFile);
			free(inspect.var.varName);
		}
		else if (inspect.type == INSPECT_IF_COND)
		{
			for (auto &cond : inspect.ifStmtConds.conds)
			{
				char *ptr = NULL, *params = NULL;
				range = cond.range;
				clang_getFileLocation(clang_getRangeStart(range), NULL, NULL, NULL, &offset);
				fwrite(&sourceBuf[prevPos], 1, offset - prevPos, outFile);
				prevPos = offset;
				if (getCallFunctionPtr(cond.cursor, &ptr, &params))
				{
					id = genId(range, filePath, ptr, "", INSPECT_FUN_PTR);
					fprintf(outFile, "%s(%u, \"%s\", %s, %s)", DEKU_INSPECT_FUN_POINTER, id, filePath, ptr, params);
					clang_getFileLocation(clang_getRangeEnd(range), NULL, NULL, NULL, &offset);
					free(ptr);
					free(params);
				}
				else
				{
					clang_getFileLocation(clang_getRangeEnd(cond.range), NULL, NULL, NULL, &offset);
					char *text = strndup(&sourceBuf[prevPos], offset - prevPos);
					CHECK_ALLOC(text);
					id = genId(range, filePath, text, "", inspect.type);
					fprintf(outFile, "%s(%u, \"%s\", %s)", DEKU_INSPECT_VAR, id, filePath, text);
					free(text);
				}
				prevPos = offset;
			}
		}
		if (inspect.type == INSPECT_FUN_PTR)
		{
			if (inspect.funPtr.varAssign)
			{
				clang_getFileLocation(clang_getRangeStart(inspect.funPtr.var.range), NULL, NULL, NULL, &offset);
				fwrite(&sourceBuf[prevPos], 1, offset - prevPos, outFile);
				prevPos = offset;
				if (!inspect.funPtr.var.init)
					fwrite("{", 1, 1, outFile);
				prevPos = offset;
			}
			range = inspect.funPtr.range;
			clang_getFileLocation(clang_getRangeStart(range), NULL, NULL, NULL, &offset);
			fwrite(&sourceBuf[prevPos], 1, offset - prevPos, outFile);
			prevPos = offset;
			id = genId(range, filePath, inspect.funPtr.ptr, "", inspect.type);
			fprintf(outFile, "%s(%u, \"%s\", %s, %s)", DEKU_INSPECT_FUN_POINTER, id, filePath, inspect.funPtr.ptr, inspect.funPtr.params);
			free(inspect.funPtr.params);
			clang_getFileLocation(clang_getRangeEnd(range), NULL, NULL, NULL, &offset);
			prevPos = offset;
			if (inspect.funPtr.varAssign)
			{
				id = genId(range, filePath, inspect.funPtr.var.varName, "", inspect.type);
				fprintf(outFile, ";%s(%u, \"%s\", %s)", DEKU_INSPECT_VAR, id, filePath, inspect.funPtr.var.varName);
				if (!inspect.funPtr.var.init)
					fwrite(";}", 1, 2, outFile);
				free(inspect.funPtr.var.varName);
			}
		}
		else if (inspect.type == INSPECT_RETURN)
		{
			range = inspect.ret.range;
			clang_getFileLocation(clang_getRangeStart(range), NULL, NULL, NULL, &offset);
			fwrite(&sourceBuf[prevPos], 1, offset - prevPos, outFile);
			prevPos = offset + strlen("return;");
			id = genId(range, filePath, inspect.ret.funName, "", inspect.type);
			fprintf(outFile, "{ %s(%u, \"%s\");return;}", DEKU_INSPECT_RETURN, id, filePath);
		}
		else if (inspect.type == INSPECT_RETURN_VALUE)
		{
			range = inspect.ret.range;
			clang_getFileLocation(clang_getRangeStart(range), NULL, NULL, NULL, &offset);
			fwrite(&sourceBuf[prevPos], 1, offset - prevPos, outFile);
			prevPos = offset + strlen("return ");
			clang_getFileLocation(clang_getRangeEnd(range), NULL, NULL, NULL, &offset);
			char *text = strndup(&sourceBuf[prevPos], offset - prevPos);
			CHECK_ALLOC(text);
			id = genId(range, filePath, inspect.ret.funName, text, inspect.type);
			fprintf(outFile, "{return %s(%u, \"%s\", %s);}", DEKU_INSPECT_RETURN_VALUE, id, filePath, text);
			prevPos = offset;
			free(text);
		}
		else if (inspect.type == INSPECT_FUNCTION)
		{
			range = inspect.func.range;
			clang_getFileLocation(inspect.func.openCurlyParen, NULL, NULL, NULL, &offset);
			clang_getFileLocation(clang_getRangeStart(range), NULL, &sLine, NULL, NULL);
			clang_getFileLocation(clang_getRangeEnd(range), NULL, &eLine, NULL, NULL);
			fwrite(&sourceBuf[prevPos], 1, offset - prevPos + 1, outFile);
			prevPos = offset + 1;
			char endLineStr[16];
			snprintf(endLineStr, sizeof(endLineStr), "%d", eLine);
			id = genId(range, filePath, inspect.func.name, endLineStr, inspect.type);
			fprintf(outFile, "%s(%u, \"%s\", %d, %d);", DEKU_INSPECT_FUNC, id, filePath, sLine, eLine);
			fprintf(outFile, "%s(current, NULL, NULL, \"%s\", __func__);", DEKU_INSPECT_STACK_TRACE, filePath);
		}
		else if (inspect.type == INSPECT_FUNCTION_END)
		{
			range = inspect.func.range;
			clang_getFileLocation(clang_getRangeEnd(range), NULL, NULL, NULL, &offset);
			fwrite(&sourceBuf[prevPos], 1, offset - prevPos - 1, outFile);
			prevPos = offset - 1;
			id = genId(range, filePath, inspect.func.name, "", inspect.type);
			fprintf(outFile, "%s(%u, \"%s\");", DEKU_INSPECT_FUN_END, id, filePath);
		}
	}
	fwrite(&sourceBuf[prevPos], 1, fSize - prevPos, outFile);
	fclose(outFile);
	clang_disposeString(srcFilePath);
}

std::vector<FunctionInspectCtx> addToFuncInspectCtxList(std::vector<FunctionInspectCtx> *funcs, FunctionInspectCtx func)
{
	if (func.inspects.size() == 0)
		return *funcs;

	std::vector<FunctionInspectCtx> result;
	int i = 0;
	for (; i < funcs->size(); i++)
	{
		if (funcs->at(i).line < func.line)
			result.push_back(funcs->at(i));
		else
			break;
	}
	result.push_back(func);
	for (; i < funcs->size(); i++)
		result.push_back(funcs->at(i));
	return result;
}

int main(int argc, char *argv[])
{
	system("echo > test.txt");
	CXIndex index = clang_createIndex(0, 0);

	if (index == 0)
	{
		fprintf(stderr, "error creating index\n");
		return 1;
	}

	char *filePath = argv[1];
	char *funcsName = argv[2];
	char *mapFile = argv[3];
	translationUnit = clang_parseTranslationUnit(index, 0,
												 argv + 4, argc - 4, NULL, 0, CXTranslationUnit_None);

	if (translationUnit == 0)
	{
		fprintf(stderr, "error creating translationUnit\n");
		return 1;
	}
	inspectMapFile = fopen(mapFile, "w");

	std::vector<FunctionInspectCtx> funcs;
	while (funcsName[0] != '\0')
	{
		FunctionInspectCtx funCtx;
		char *comma = strchr(funcsName, ',');
		if (comma)
		{
			*comma = '\0';
			funCtx.name = funcsName;
			getInspectionsForFunction(&funCtx);
			funcs = addToFuncInspectCtxList(&funcs, funCtx);
			funcsName = comma + 1;
		}
		else
		{
			funCtx.name = funcsName;
			getInspectionsForFunction(&funCtx);
			funcs = addToFuncInspectCtxList(&funcs, funCtx);
			break;
		}
	}
	if (funcs.size())
	{
		std::vector<Inspect> inspects;
		for (FunctionInspectCtx &fun : funcs)
		{
			for (auto &inspect : fun.inspects)
				inspects.push_back(inspect);
		}
		applyInspections(&inspects, filePath);
	}

	clang_disposeTranslationUnit(translationUnit);
	clang_disposeIndex(index);
	fclose(inspectMapFile);

	return 0;
}
