#include <clang-c/Index.h>
#include <fstream>
#include <string>

class String {
  CXString m_str;
public:
  String(CXString str) : m_str(str) {}
  ~String() { clang_disposeString(m_str); }

  operator const char*() { return clang_getCString(m_str); }
};

std::string &operator+=(std::string &str, unsigned int i) {
  static char buf[15] = { '\0' };
  char *ptr = &buf[13];
  do {
    *ptr-- = (i % 10) + '0';
    i = i/10;
  } while (i > 0);
  return str += (ptr + 1);
}

std::ofstream sql_output;

CXChildVisitResult mainVisitor(CXCursor cursor, CXCursor parent, void *data);

int main(int argc, char **argv) {
  CXIndex idx = clang_createIndex(0, 0);
  CXTranslationUnit contents = clang_parseTranslationUnit(idx,
    NULL, argv, argc, NULL, 0, CXTranslationUnit_None);

  // We are going to output to file.sql
  CXString ifile = clang_getTranslationUnitSpelling(contents);
  std::string ofile(clang_getCString(ifile));
  ofile += ".sql";
  sql_output.open(ofile.c_str());
  clang_disposeString(ifile);

  CXCursor mainCursor = clang_getTranslationUnitCursor(contents);
  clang_visitChildren(mainCursor, mainVisitor, NULL);


  // Clean everything up!
  sql_output.close();
  clang_disposeTranslationUnit(contents);
  clang_disposeIndex(idx);
  return 0;
}

bool wantLocation(CXSourceLocation loc) {
  CXFile file;
  clang_getSpellingLocation(loc, &file, NULL, NULL, NULL);
  String locStr(clang_getFileName(file));
  if ((const char *)locStr == NULL)
    return false;
  std::string cxxstr((const char*)locStr);
  if (cxxstr[0] != '/') // Relative -> probably in srcdir
    return true;
  return cxxstr.find("/src/mozilla-tools/clang-dxr-noplugin") == 0;
}

std::string sanitizeString(std::string &str) {
  std::string result(str);
  size_t iter = -1;
  while ((iter = result.find('\'', iter + 1)) != std::string::npos) {
    result.replace(iter, 1, "''");
  }
  return result;
}

std::string cursorLocation(CXCursor cursor) {
  CXSourceLocation sourceLoc = clang_getCursorLocation(cursor);
  CXFile file;
  unsigned int line, column;
  clang_getSpellingLocation(sourceLoc, &file, &line, &column, NULL);
  String filestring(clang_getFileName(file));
  std::string fstr;
  if (filestring == NULL)
    fstr += "(null)";
  else
    fstr += filestring;
  std::string result = sanitizeString(fstr);
  result += ":";
  result += line;
  result += ":";
  result += column;
  return result;
}

std::string getFQName(CXType type);
std::string getFQName(CXCursor cursor);

static CXChildVisitResult fqbld(CXCursor child, CXCursor parent, void *param) {
  std::string &build = *(std::string*)param;
  CXCursorKind kind = clang_getCursorKind(child);
  if (kind != CXCursor_ParmDecl)
    return CXChildVisit_Break;
  CXType type = clang_getCursorType(child);
  build += getFQName(type);
  build += ", ";
  return CXChildVisit_Continue;
}

std::string getFQName(CXCursor cursor) {
  CXCursor parent = clang_getCursorSemanticParent(cursor);
  if (clang_equalCursors(parent, clang_getNullCursor()))
    return std::string();
  std::string base = getFQName(parent);
  if (!base.empty())
    base += "::";
  String component(clang_getCursorSpelling(cursor));
  if ((const char *)component != NULL)
    base += component;
  CXCursorKind kind = clang_getCursorKind(cursor);
  if (kind == CXCursor_FunctionDecl || kind == CXCursor_CXXMethod ||
      kind == CXCursor_Constructor || kind == CXCursor_Destructor ||
      kind == CXCursor_ConversionFunction) {
    // This is a method
    base += "(";
    clang_visitChildren(cursor, fqbld, &base);
    if (*(base.end() - 1) == '(')
      base += ")";
    else
      base.replace(base.end() - 2, base.end(), ")");
  }
  return base;
}

const char *primitiveTypes[] = {
  "void", "bool", "char", "unsigned char", "char16_t", "char32_t",
  "unsigned short", "unsigned int", "unsigned long", "unsigned long long",
  "__uint128_t", "char", "signed char", "wchar_t", "int", "long", "long long",
  "__int128_t", "float", "double", "long double", "std::nullptr_t", NULL, NULL,
  NULL, NULL, NULL};

std::string getFQName(CXType type) {
  // Normalize types in case of namespace aliasing or other weird stuff
  // Just don't undo typedefs
  if (type.kind != CXType_Typedef)
    type = clang_getCanonicalType(type);
  std::string typeStr;
  if (type.kind >= CXType_FirstBuiltin && type.kind <= CXType_LastBuiltin) {
    if (clang_isConstQualifiedType(type))
      typeStr += "const ";
    if (clang_isVolatileQualifiedType(type))
      typeStr += "volatile ";
    typeStr += primitiveTypes[type.kind - CXType_FirstBuiltin];
    return typeStr;
  }
  switch (type.kind) {
    case CXType_Pointer:
      typeStr += getFQName(clang_getPointeeType(type));
      typeStr += "*";
      if (clang_isConstQualifiedType(type))
        typeStr += " const";
      if (clang_isVolatileQualifiedType(type))
        typeStr += " volatile";
      break;
    case CXType_Enum:
    case CXType_Typedef:
      return getFQName(clang_getTypeDeclaration(type));
    default: {
      String kindStr(clang_getTypeKindSpelling(type.kind));
      printf("Kind of type: %s\n", (const char *)kindStr);
    }
  }
  return typeStr;
}

void processCompoundType(CXCursor cursor, CXCursorKind kind) {
  const char *kindName;
  if (kind == CXCursor_StructDecl)
    kindName = "struct";
  else if (kind == CXCursor_UnionDecl)
    kindName = "union";
  else if (kind == CXCursor_ClassDecl)
    kindName = "class";
  else if (kind == CXCursor_EnumDecl)
    kindName = "enum";
  else
    kindName = "___UNKNOWN___";
  // XXX: Need to support namespaces
  sql_output << "INSERT INTO types ('tname', 'tloc', 'tkind') VALUES ('" <<
    getFQName(cursor) << "', '" << cursorLocation(cursor) << "', '" <<
    kindName << "');" << std::endl;
}

void processFunctionType(CXCursor cursor, CXCursorKind kind) {
  CXCursor container = clang_getCursorSemanticParent(cursor);
  CXCursorKind parentKind = clang_getCursorKind(container);
  bool isContained = (parentKind != CXCursor_Namespace &&
      parentKind != CXCursor_TranslationUnit &&
      parentKind != CXCursor_UnexposedDecl); // Anonymous namespace ?
  sql_output << "INSERT INTO members (";
  if (isContained)
    sql_output << "'mtname', 'mtdecl', ";
  sql_output << "'mname', 'mshortname', 'mdecl') VALUES ('";
  if (isContained)
    sql_output << getFQName(container) << "', '" <<
      cursorLocation(container) << "', '";
  String shortname(clang_getCursorSpelling(cursor));
  sql_output << getFQName(cursor) << "', '" << (shortname) << "', '" <<
    cursorLocation(cursor) << "');" << std::endl;
}

void processVariableType(CXCursor cursor, CXCursorKind kind) {
  // Is this an included method or not?
  CXCursor container = clang_getCursorSemanticParent(cursor);
  // XXX: broken!
  //if (clang_equalCursors(container, clang_getNullCursor()))
  //  fprintf(stderr, "No parent!\n");
  String name(clang_getCursorSpelling(cursor));
  sql_output << "INSERT INTO members ('mname', 'mdecl') VALUES ('" <<
    (name) << "', '" <<
    cursorLocation(cursor) << "');" << std::endl;
}

CXChildVisitResult mainVisitor(CXCursor cursor, CXCursor parent, void *data) {
  CXCursorKind kind = clang_getCursorKind(cursor);

  // Step 1: Do we care about this location?
  CXSourceLocation sourceLoc = clang_getCursorLocation(cursor);
  if (!wantLocation(sourceLoc))
    return CXChildVisit_Continue;

  // Dispatch the code to the main processors
  if (!clang_isDeclaration(kind))
    return CXChildVisit_Continue;
  switch (kind) {
    case CXCursor_StructDecl:
    case CXCursor_UnionDecl:
    case CXCursor_ClassDecl:
    case CXCursor_EnumDecl:
      processCompoundType(cursor, kind);
      return CXChildVisit_Recurse;
    case CXCursor_FunctionDecl:
    case CXCursor_CXXMethod:
    case CXCursor_Constructor:
    case CXCursor_Destructor:
    case CXCursor_ConversionFunction:
      processFunctionType(cursor, kind);
      return CXChildVisit_Recurse;
    case CXCursor_FieldDecl:
    case CXCursor_EnumConstantDecl:
    case CXCursor_VarDecl:
    case CXCursor_ParmDecl:
      processVariableType(cursor, kind);
      return CXChildVisit_Recurse;
    case CXCursor_UnexposedDecl:
    case CXCursor_Namespace:
      return CXChildVisit_Recurse;
    default: {
      String kindSpell(clang_getCursorKindSpelling(kind));
      fprintf(stderr, "Unknown kind: %s at %s\n", (const char *)kindSpell,
        cursorLocation(cursor).c_str());
    }
  }
  return CXChildVisit_Recurse;
}
