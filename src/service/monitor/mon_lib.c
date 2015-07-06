#ifdef __cplusplus
extern "C"{
#endif

#include <dos.h>

#if (INCLUDE_BH_SERVER)
#if INCLUDE_RES_MONITOR

#include "mon_lib.h"

#ifndef MAX_PID_VALUE
#define MAX_PID_VALUE 65535
#define MIN_PID_VALUE 0
#endif

extern S8 * g_pszAnalyseList;

/**
 * 功能:为字符串分配内存
 * 参数集：
 *   无参数
 * 返回值：
 *   成功返回DOS_SUCC，失败返回DOS_FAIL
 */
U32  mon_init_str_array()
{
   g_pszAnalyseList = (S8 *)dos_dmem_alloc(MAX_TOKEN_CNT * MAX_TOKEN_LEN * sizeof(S8));
   if(DOS_ADDR_INVALID(g_pszAnalyseList))
   {
      logr_error("%s:Line %u:mon_init_str_array|initialize string array failure!"
                    , dos_get_filename(__FILE__), __LINE__);
      return DOS_FAIL;
   }
   return DOS_SUCC;
}

/**
 * 功能:释放为字符串分配的内存
 * 参数集：
 *   无参数
 * 返回值：
 *   成功返回DOS_SUCC，失败返回DOS_FAIL
 */
U32  mon_deinit_str_array()
{
   if(DOS_ADDR_INVALID(g_pszAnalyseList))
   {
      logr_error("%s:Line %u:mon_deinit_str_array|initialize string array failure!"
                    , dos_get_filename(__FILE__), __LINE__);
      return DOS_FAIL;
   }
   dos_dmem_free(g_pszAnalyseList);
   g_pszAnalyseList = NULL;
   return DOS_SUCC;
}

/**
 * 功能:判断pszDest是否为pszSrc的子串
 * 参数集：
 *   参数1:S8 * pszSrc  源字符串
 *   参数2:S8 * pszDest 子字符串
 * 返回值：
 *   是则返回DOS_TRUE，否则返回DOS_FALSE
 */
BOOL mon_is_substr(S8 * pszSrc, S8 * pszDest)
{
   S8 * pszStr = pszSrc;

   if(DOS_ADDR_INVALID(pszSrc)
      || DOS_ADDR_INVALID(pszDest))
   {
      logr_warning("%s:Line %u:mon_is_substr|pszSrc is %p,pszDest is %p!"
                    , dos_get_filename(__FILE__), __LINE__, pszSrc, pszDest);
      return DOS_FALSE;
   }

   /* 如果pszSrc的长度小于pszDest，那么一定不是子串 */
   if (dos_strlen(pszSrc) < dos_strlen(pszDest))
   {
      logr_warning("%s:Line %u:mon_is_substr|dos_strlen(pszSrc) = %u,dos_strlen(pszDest) = %u!"
                    , dos_get_filename(__FILE__), __LINE__, dos_strlen(pszSrc)
                    , dos_strlen(pszDest));
      return DOS_FALSE;
   }

   for (;pszStr <= pszSrc + dos_strlen(pszSrc) - dos_strlen(pszDest); ++pszStr)
   {
      if (*pszStr == *pszDest)
      {
         if (dos_strncmp(pszStr, pszDest, dos_strlen(pszDest)) == 0)
         {
            return DOS_TRUE;
         }
      }
   }

   return DOS_FALSE;
}

/**
 * 功能:判断pszSentence是否以pszStr结尾
 * 参数集：
 *   参数1:S8 * pszSentence  源字符串
 *   参数2:S8 * pszStr       结尾字符串
 * 返回值：
 *   是则返回DOS_TRUE，否则返回DOS_FALSE
 */
BOOL mon_is_ended_with(S8 * pszSentence, const S8 * pszStr)
{
   S8 *pszSrc = NULL, *pszTemp = NULL;

   if(DOS_ADDR_INVALID(pszSentence)
      || DOS_ADDR_INVALID(pszStr))
   {
      logr_warning("%s:Line %u:mon_is_ended_with|pszSentence is %p,pszStr is %p!"
                    , dos_get_filename(__FILE__), __LINE__, pszSentence, pszStr);
      return DOS_FALSE;
   }

   if (dos_strlen(pszSentence) < dos_strlen(pszStr))
   {
      logr_warning("%s:Line %u:mon_is_ended_with|dos_strlen(pszSentence) = %u,dos_strlen(pszStr) = %u!"
                    , dos_strlen(pszStr), dos_strlen(pszStr));
      return DOS_FALSE;
   }
   pszSrc = pszSentence + dos_strlen(pszSentence) - dos_strlen(pszStr);
   pszTemp = (S8 *)pszStr;

   while (*pszTemp != '\0')
   {
      if (*pszSrc != *pszTemp)
      {
         logr_notice("%s:Line %u:mon_is_ended_with|\'%s\' is not ended with \'%s\'!"
                        , dos_get_filename(__FILE__), __LINE__, pszSrc, pszTemp);
         return DOS_FALSE;
      }
      ++pszSrc;
      ++pszTemp;
   }

   return DOS_TRUE;
}

/**
 * 功能:判断pszFile是否以pszSuffix为文件后缀名
 * 参数集：
 *   参数1:S8 * pszFile   源文件名
 *   参数2:S8 * pszSuffix 文件后缀名
 * 返回值：
 *   是则返回DOS_TRUE，否则返回DOS_FALSE
 */
BOOL mon_is_suffix_true(S8 * pszFile, const S8 * pszSuffix)
{
   S8 * pszFileSrc = NULL;
   S8 * pszSuffixSrc = NULL;

   if(DOS_ADDR_INVALID(pszFile)
        || DOS_ADDR_INVALID(pszFile))
   {
      logr_warning("%s:Line %u:mon_is_suffix_true|pszFile is %p, pszSuffix is %p!"
                    , dos_get_filename(__FILE__), __LINE__, pszFile, pszSuffix);
      return DOS_FALSE;
   }

   pszFileSrc = pszFile + dos_strlen(pszFile) -1;
   pszSuffixSrc = (S8 *)pszSuffix + dos_strlen(pszSuffix) - 1;

   for (; pszSuffixSrc >= pszSuffix; pszSuffixSrc--, pszFileSrc--)
   {
      if (*pszFileSrc != *pszSuffixSrc)
      {
         logr_notice("%s:Line %u:mon_is_suffix_true|the suffix of \'%s\' is not \'%s\'!"
                        ,dos_get_filename(__FILE__), __LINE__, pszFile, pszSuffix);
         return DOS_FALSE;
      }
   }

   return DOS_TRUE;
}

/**
 * 功能:获取字符串中的一个整数
 * 参数集：
 *   参数1:S8 * pszStr  含有数字字符串的字符串
 * 返回值：
 *   成功则返回字符串中的的一个数字，失败则返回DOS_FAIL
 */
U32 mon_first_int_from_str(S8 * pszStr)
{
   U32  ulData;
   S8   szTail[1024] = {0};
   S8 * pszSrc = pszStr;
   S8 * pszTemp = NULL;
   S32  lRet = 0;

   if(DOS_ADDR_INVALID(pszStr))
   {
      logr_warning("%s:Line %u:mon_first_int_from_str|pszStr is %p!"
                    , dos_get_filename(__FILE__), __LINE__, pszStr);
      return DOS_FAIL;
   }

   while (!(*pszSrc >= '0' && *pszSrc <= '9'))
   {
      ++pszSrc;
   }
   pszTemp = pszSrc;
   while (*pszTemp >= '0' && *pszTemp <= '9')
   {
      pszTemp++;
   }
   dos_strncpy(szTail, pszSrc, pszTemp - pszSrc);
   szTail[pszTemp - pszSrc] = '\0';

   lRet = dos_atoul(szTail, &ulData);
   if(0 != lRet)
   {
      logr_error("%s:Line %u:mon_first_int_from_str|dos_atol failure,lRet is %d!"
                    , dos_get_filename(__FILE__), __LINE__, lRet);
      return DOS_FAIL;
   }

   return ulData;
}

/**
 * 功能:将字符串pszStr按照pszRegExpr规则去分割，并存储到pszRsltList中l
 * 参数集：
 *   参数1:S8 * pszStr       含有数字字符串的字符串
 *   参数2:S8* pszRegExpr    分界字符串
 *   参数3:S8* pszRsltList[] 用于存放字符串的首地址
 *   参数4:U32 ulLen         数组最大长度
 * 返回值：
 *   成功则返回DOS_SUCC，失败则返回DOS_FAIL
 */
U32  mon_analyse_by_reg_expr(S8* pszStr, S8* pszRegExpr, S8* pszRsltList[], U32 ulLen)
{
   U32 ulRows = 0;
   S8* pszToken = NULL;

   if(DOS_ADDR_INVALID(pszStr))
   {
      logr_error("%s:Line %u:mon_analyse_by_reg_expr|pszStr is %p!"
                    , dos_get_filename(__FILE__), __LINE__);
      return DOS_FAIL;
   }

   if(DOS_ADDR_INVALID(pszRegExpr))
   {
      logr_error("%s:Line %u:mon_analyse_by_reg_expr|pszRegExpr is %p!"
                    , dos_get_filename(__FILE__), __LINE__);
      return DOS_FAIL;
   }

   /* 每次使用前初始化为0 */
   memset(g_pszAnalyseList, 0, MAX_TOKEN_CNT * MAX_TOKEN_LEN * sizeof(char));



   for(ulRows = 0; ulRows < ulLen; ulRows++)
   {
      /*把字符串首地址分别置于分配内存的相应位置*/
      pszRsltList[ulRows] = g_pszAnalyseList + ulRows * MAX_TOKEN_LEN;
   }

   for(ulRows = 0; ulRows < ulLen; ulRows++)
   {
      S8 *pszBuff = NULL;
      if(0 == ulRows)
      {
         pszBuff = pszStr;
      }

      pszToken = strtok(pszBuff, pszRegExpr);
      if(DOS_ADDR_INVALID(pszToken))
      {
         break;
      }
      dos_strncpy(pszRsltList[ulRows], pszToken, dos_strlen(pszToken));
      pszRsltList[ulRows][dos_strlen(pszToken)] = '\0';
   }

   return DOS_SUCC;
}

/**
 * 功能:生成告警id
 * 参数集：
 *   参数1:U32 ulResType     资源种类
 *   参数2:U32 ulNo          资源编号
 *   参数3:U32 ulErrType     错误类型
 * 返回值：
 *   成功则返回告警id，失败则返回(U32)0xff
 */
U32 mon_generate_warning_id(U32 ulResType, U32 ulNo, U32 ulErrType)
{
   if(ulResType >= (U32)0xff || ulNo >= (U32)0xff
      || ulErrType >= (U32)0xff)
   {
      logr_notice("%s:Line %u: mon_generate_warning_id|ulResType is %s%x,ulNo is %s%x, ulErrType is %s%x"
                    , dos_get_filename(__FILE__), __LINE__, "0x", ulResType, "0x", ulNo, "0x", ulErrType);
      return (U32)0xff;
   }
   /* 第1个8位存储资源类型，第2个8位存储资源编号，第3个8位存储错误编号 */
   return (ulResType << 24) | (ulNo << 16 ) | (ulErrType & 0xffffffff);
}


/**
 * 功能:为字符串去头去尾，只留下最简单的名字
 * 参数集：
 *   参数1:S8 * pszCmd   进程启动命令
 * 返回值：
 *   成功则返回去头去尾之后的简单名称，失败则返回NULL
 */
S8 * mon_str_get_name(S8 * pszCmd)
{
   S8 * pszPtr = pszCmd;
   if(DOS_ADDR_INVALID(pszPtr))
   {
      logr_error("%s:Line %u:mon_str_get_cmd|get cmd failure,pszCmd is %p!"
                    , dos_get_filename(__FILE__), __LINE__, pszCmd);
      return NULL;
   }

   /**
    *  找到第一个空格，前面为命令的绝对路径，后面为命令附带的相关参数
    */
   while(*pszPtr != ' ' && *pszPtr != '\0' && *pszPtr != '\t')
   {
      ++pszPtr;
   }
   *pszPtr = '\0';

   /*找到最后一个'/'字符，'/'和' '之间的部分是命令的最简化名称*/
   while(*(pszPtr - 1) != '/' && pszPtr != pszCmd)
   {
      --pszPtr;
   }

   pszCmd = pszPtr;

   return pszCmd;
}

#endif //#if INCLUDE_RES_MONITOR
#endif //#if (INCLUDE_BH_SERVER)

#ifdef __cplusplus
}
#endif

