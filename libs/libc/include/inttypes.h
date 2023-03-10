#ifndef _INTTYPES_H
#define _INTTYPES_H 1

#include <bits/wchar_t.h>
#include <stdint.h>

#ifdef __x86_64__
#define __PRI8   ""
#define __PRI16  ""
#define __PRI32  ""
#define __PRI64  "l"
#define __PRIPTR __PRI64

#define __SCN8   "hh"
#define __SCN16  "h"
#define __SCN32  ""
#define __SCN64  "l"
#define __SCNPTR __SCN64
#elif defined(__i386__)
#define __PRI8   ""
#define __PRI16  ""
#define __PRI32  ""
#define __PRI64  "ll"
#define __PRIPTR __PRI32

#define __SCN8   "hh"
#define __SCN16  "h"
#define __SCN32  ""
#define __SCN64  "ll"
#define __SCNPTR __SCN32
#endif

#define PRId8 __PRI8 "d"
#define PRIi8 __PRI8 "i"
#define PRIo8 __PRI8 "o"
#define PRIu8 __PRI8 "u"
#define PRIx8 __PRI8 "x"
#define PRIX8 __PRI8 "X"

#define PRIdLEAST8 __PRI32 "d"
#define PRIiLEAST8 __PRI32 "i"
#define PRIoLEAST8 __PRI32 "o"
#define PRIuLEAST8 __PRI32 "u"
#define PRIxLEAST8 __PRI32 "x"
#define PRIXLEAST8 __PRI32 "X"

#define PRIdFAST8 __PRI32 "d"
#define PRIiFAST8 __PRI32 "i"
#define PRIoFAST8 __PRI32 "o"
#define PRIuFAST8 __PRI32 "u"
#define PRIxFAST8 __PRI32 "x"
#define PRIXFAST8 __PRI32 "X"

#define PRId16 __PRI16 "d"
#define PRIi16 __PRI16 "i"
#define PRIo16 __PRI16 "o"
#define PRIu16 __PRI16 "u"
#define PRIx16 __PRI16 "x"
#define PRIX16 __PRI16 "X"

#define PRIdLEAST16 __PRI32 "d"
#define PRIiLEAST16 __PRI32 "i"
#define PRIoLEAST16 __PRI32 "o"
#define PRIuLEAST16 __PRI32 "u"
#define PRIxLEAST16 __PRI32 "x"
#define PRIXLEAST16 __PRI32 "X"

#define PRIdFAST16 __PRI32 "d"
#define PRIiFAST16 __PRI32 "i"
#define PRIoFAST16 __PRI32 "o"
#define PRIuFAST16 __PRI32 "u"
#define PRIxFAST16 __PRI32 "x"
#define PRIXFAST16 __PRI32 "X"

#define PRId32 __PRI32 "d"
#define PRIi32 __PRI32 "i"
#define PRIo32 __PRI32 "o"
#define PRIu32 __PRI32 "u"
#define PRIx32 __PRI32 "x"
#define PRIX32 __PRI32 "X"

#define PRIdLEAST32 __PRI32 "d"
#define PRIiLEAST32 __PRI32 "i"
#define PRIoLEAST32 __PRI32 "o"
#define PRIuLEAST32 __PRI32 "u"
#define PRIxLEAST32 __PRI32 "x"
#define PRIXLEAST32 __PRI32 "X"

#define PRIdFAST32 __PRI32 "d"
#define PRIiFAST32 __PRI32 "i"
#define PRIoFAST32 __PRI32 "o"
#define PRIuFAST32 __PRI32 "u"
#define PRIxFAST32 __PRI32 "x"
#define PRIXFAST32 __PRI32 "X"

#define PRId64 __PRI64 "d"
#define PRIi64 __PRI64 "i"
#define PRIo64 __PRI64 "o"
#define PRIu64 __PRI64 "u"
#define PRIx64 __PRI64 "x"
#define PRIX64 __PRI64 "X"

#define PRIdLEAST64 __PRI64 "d"
#define PRIiLEAST64 __PRI64 "i"
#define PRIoLEAST64 __PRI64 "o"
#define PRIuLEAST64 __PRI64 "u"
#define PRIxLEAST64 __PRI64 "x"
#define PRIXLEAST64 __PRI64 "X"

#define PRIdFAST64 __PRI64 "d"
#define PRIiFAST64 __PRI64 "i"
#define PRIoFAST64 __PRI64 "o"
#define PRIuFAST64 __PRI64 "u"
#define PRIxFAST64 __PRI64 "x"
#define PRIXFAST64 __PRI64 "X"

#define PRIdMAX __PRI64 "d"
#define PRIiMAX __PRI64 "i"
#define PRIoMAX __PRI64 "o"
#define PRIuMAX __PRI64 "u"
#define PRIxMAX __PRI64 "x"
#define PRIXMAX __PRI64 "X"

#define PRIdPTR __PRIPTR "d"
#define PRIiPTR __PRIPTR "i"
#define PRIoPTR __PRIPTR "o"
#define PRIuPTR __PRIPTR "u"
#define PRIxPTR __PRIPTR "x"
#define PRIXPTR __PRIPTR "X"

#define SCNd8 __SCN8 "d"
#define SCNi8 __SCN8 "i"
#define SCNo8 __SCN8 "o"
#define SCNu8 __SCN8 "u"
#define SCNx8 __SCN8 "x"

#define SCNdLEAST8 __SCN32 "d"
#define SCNiLEAST8 __SCN32 "i"
#define SCNoLEAST8 __SCN32 "o"
#define SCNuLEAST8 __SCN32 "u"
#define SCNxLEAST8 __SCN32 "x"

#define SCNdFAST8 __SCN32 "d"
#define SCNiFAST8 __SCN32 "i"
#define SCNoFAST8 __SCN32 "o"
#define SCNuFAST8 __SCN32 "u"
#define SCNxFAST8 __SCN32 "x"

#define SCNd16 __SCN16 "d"
#define SCNi16 __SCN16 "i"
#define SCNo16 __SCN16 "o"
#define SCNu16 __SCN16 "u"
#define SCNx16 __SCN16 "x"

#define SCNdLEAST16 __SCN32 "d"
#define SCNiLEAST16 __SCN32 "i"
#define SCNoLEAST16 __SCN32 "o"
#define SCNuLEAST16 __SCN32 "u"
#define SCNxLEAST16 __SCN32 "x"

#define SCNdFAST16 __SCN32 "d"
#define SCNiFAST16 __SCN32 "i"
#define SCNoFAST16 __SCN32 "o"
#define SCNuFAST16 __SCN32 "u"
#define SCNxFAST16 __SCN32 "x"

#define SCNd32 __SCN32 "d"
#define SCNi32 __SCN32 "i"
#define SCNo32 __SCN32 "o"
#define SCNu32 __SCN32 "u"
#define SCNx32 __SCN32 "x"

#define SCNdLEAST32 __SCN32 "d"
#define SCNiLEAST32 __SCN32 "i"
#define SCNoLEAST32 __SCN32 "o"
#define SCNuLEAST32 __SCN32 "u"
#define SCNxLEAST32 __SCN32 "x"

#define SCNdFAST32 __SCN32 "d"
#define SCNiFAST32 __SCN32 "i"
#define SCNoFAST32 __SCN32 "o"
#define SCNuFAST32 __SCN32 "u"
#define SCNxFAST32 __SCN32 "x"

#define SCNd64 __SCN64 "d"
#define SCNi64 __SCN64 "i"
#define SCNo64 __SCN64 "o"
#define SCNu64 __SCN64 "u"
#define SCNx64 __SCN64 "x"

#define SCNdLEAST64 __SCN64 "d"
#define SCNiLEAST64 __SCN64 "i"
#define SCNoLEAST64 __SCN64 "o"
#define SCNuLEAST64 __SCN64 "u"
#define SCNxLEAST64 __SCN64 "x"

#define SCNdFAST64 __SCN64 "d"
#define SCNiFAST64 __SCN64 "i"
#define SCNoFAST64 __SCN64 "o"
#define SCNuFAST64 __SCN64 "u"
#define SCNxFAST64 __SCN64 "x"

#define SCNdMAX __SCN64 "d"
#define SCNiMAX __SCN64 "i"
#define SCNoMAX __SCN64 "o"
#define SCNuMAX __SCN64 "u"
#define SCNxMAX __SCN64 "x"

#define SCNdPTR __SCNPTR "d"
#define SCNiPTR __SCNPTR "i"
#define SCNoPTR __SCNPTR "o"
#define SCNuPTR __SCNPTR "u"
#define SCNxPTR __SCNPTR "x"

intmax_t strtoimax(const char *__restrict str, char **__restrict endptr, int base);
uintmax_t strtoumax(const char *__restrict str, char **__restrict endptr, int base);

#endif /* _INTTYPES_H */
