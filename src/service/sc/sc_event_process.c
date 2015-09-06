/*
 *            (C) Copyright 2014, DIPCC . Co., Ltd.
 *                    ALL RIGHTS RESERVED
 *
 *  文件名: sc_event_process.c
 *
 *  创建时间: 2015年1月5日16:18:41
 *  作    者: Larry
 *  描    述: 处理FS核心发过来的各种事件
 *  修改历史:
 */

#ifdef __cplusplus
extern "C"{
#endif /* __cplusplus */


#include <dos.h>
#include <esl.h>
#include <sys/time.h>
#include <pthread.h>
#include <bs_pub.h>
#include <libcurl/curl.h>
#include "sc_def.h"
#include "sc_debug.h"
#include "sc_acd_def.h"
#include "sc_ep.h"
#include "sc_acd_def.h"
#include "sc_http_api.h"

/* 应用外部变量 */
extern DB_HANDLE_ST         *g_pstSCDBHandle;

extern SC_TASK_MNGT_ST      *g_pstTaskMngtInfo;

/* ESL 句柄维护 */
SC_EP_HANDLE_ST          *g_pstHandle = NULL;
pthread_mutex_t          g_mutexEventList = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t           g_condEventList = PTHREAD_COND_INITIALIZER;

/* 事件队列 REFER TO SC_EP_EVENT_NODE_ST */
DLL_S                    g_stEventList;

/* 号码变换数据链表 REFER TO SC_NUM_TRANSFORM_NODE_ST */
DLL_S                    g_stNumTransformList;
pthread_mutex_t          g_mutexNumTransformList = PTHREAD_MUTEX_INITIALIZER;

/* 路由数据链表 REFER TO SC_ROUTE_NODE_ST */
DLL_S                    g_stRouteList;
pthread_mutex_t          g_mutexRouteList = PTHREAD_MUTEX_INITIALIZER;

/* 网关列表 refer to SC_GW_NODE_ST (使用HASH) */
HASH_TABLE_S             *g_pstHashGW = NULL;
pthread_mutex_t          g_mutexHashGW = PTHREAD_MUTEX_INITIALIZER;

/* 网关组列表， refer to SC_GW_GRP_NODE_ST (使用HASH) */
HASH_TABLE_S             *g_pstHashGWGrp = NULL;
pthread_mutex_t          g_mutexHashGWGrp = PTHREAD_MUTEX_INITIALIZER;

/* TT号和EIX对应关系表 */
HASH_TABLE_S             *g_pstHashTTNumber = NULL;
pthread_mutex_t          g_mutexHashTTNumber = PTHREAD_MUTEX_INITIALIZER;/* 主叫号码列表 ，参照 SC_CALLER_QUERY_NODE_ST */
HASH_TABLE_S             *g_pstHashCaller = NULL;
pthread_mutex_t          g_mutexHashCaller = PTHREAD_MUTEX_INITIALIZER;

/* 主叫号码组列表，参照SC_CALLER_GRP_NODE_ST */
HASH_TABLE_S             *g_pstHashCallerGrp = NULL;
pthread_mutex_t          g_mutexHashCallerGrp = PTHREAD_MUTEX_INITIALIZER;


/* 主叫号码设定列表，参照SC_CALLER_SETTING_ST */
HASH_TABLE_S             *g_pstHashCallerSetting = NULL;
pthread_mutex_t          g_mutexHashCallerSetting = PTHREAD_MUTEX_INITIALIZER;

/*
 * 网关组和网关内存中的结构:
 * 使用两个hash表存放网关和中网关组
 * 每个网关组节点中使用双向链表将属于当前网关组的网关存储起来，
 * 而每一双向链表节点的数据域是存储网关hash表hash节点的数据域
 */

/* SIP账户HASH表 REFER TO SC_USER_ID_NODE_ST */
HASH_TABLE_S             *g_pstHashSIPUserID  = NULL;
pthread_mutex_t          g_mutexHashSIPUserID = PTHREAD_MUTEX_INITIALIZER;

/* DID号码hash表 REFER TO SC_DID_NODE_ST */
HASH_TABLE_S             *g_pstHashDIDNum  = NULL;
pthread_mutex_t          g_mutexHashDIDNum = PTHREAD_MUTEX_INITIALIZER;

/* 黑名单HASH表 */
HASH_TABLE_S             *g_pstHashBlackList  = NULL;
pthread_mutex_t          g_mutexHashBlackList = PTHREAD_MUTEX_INITIALIZER;

/* 企业链表 */
DLL_S                    g_stCustomerList;
pthread_mutex_t          g_mutexCustomerList = PTHREAD_MUTEX_INITIALIZER;

/* 主叫号码限制hash表 refer to SC_NUMBER_LMT_NODE_ST */
HASH_TABLE_S             *g_pstHashNumberlmt = NULL;
pthread_mutex_t          g_mutexHashNumberlmt = PTHREAD_MUTEX_INITIALIZER;

CURL *g_pstCurlHandle;

SC_EP_TASK_CB            g_astEPTaskList[SC_EP_TASK_NUM];


U32                      g_ulCPS                  = SC_MAX_CALL_PRE_SEC;
U32                      g_ulMaxConcurrency4Task  = SC_MAX_CALL / 3;

extern U32 py_exec_func(const char * pszModule,const char * pszFunc,const char * pszPyFormat,...);
SC_EP_MSG_STAT_ST        g_astEPMsgStat[2];

/**
 * 函数: sc_ep_call_notify
 * 功能: 通知坐席弹屏
 * 参数:
 *    SC_ACD_AGENT_INFO_ST *pstAgentInfo, S8 *szCaller
 * 返回值:
 *    成功返回DOS_SUCC， 否则返回DOS_FAIL
 */
U32 sc_ep_call_notify(SC_ACD_AGENT_INFO_ST *pstAgentInfo, S8 *szCaller)
{
    S8 szURL[256] = { 0, };
    S8 szData[512] = { 0, };
    U32 ulTimeout = 2;
    U32 ulRet = 0;

    if (DOS_ADDR_INVALID(pstAgentInfo)
        || DOS_ADDR_INVALID(szCaller))
    {
        DOS_ASSERT(0);

        return DOS_FAIL;
    }

    if (DOS_ADDR_INVALID(g_pstCurlHandle))
    {
        g_pstCurlHandle = curl_easy_init();
        if (DOS_ADDR_INVALID(g_pstCurlHandle))
        {
            DOS_ASSERT(0);

            return DOS_FAIL;
        }
    }

    dos_snprintf(szURL, sizeof(szURL), "http://localhost/pub?id=%d_%s_%s"
                , pstAgentInfo->ulSiteID
                , pstAgentInfo->szEmpNo
                , pstAgentInfo->szExtension);

    /* 格式中引号前面需要添加"\",提供给push stream做转义用 */
    dos_snprintf(szData, sizeof(szData), "{\\\"status\\\":\\\"0\\\",\\\"number\\\":\\\"%s\\\"}", szCaller);

    curl_easy_reset(g_pstCurlHandle);
    curl_easy_setopt(g_pstCurlHandle, CURLOPT_URL, szURL);
    curl_easy_setopt(g_pstCurlHandle, CURLOPT_POSTFIELDS, szData);
    curl_easy_setopt(g_pstCurlHandle, CURLOPT_TIMEOUT, ulTimeout);
    ulRet = curl_easy_perform(g_pstCurlHandle);
    if(CURLE_OK != ulRet)
    {
        DOS_ASSERT(0);

        sc_logr_notice(SC_ESL, "CURL post FAIL.Caller:%s.", szData);
        return DOS_FAIL;
    }
    else
    {
        sc_logr_notice(SC_ESL, "CURL post SUCC.Caller:%s.", szData);

        return DOS_SUCC;
    }
}

/**
 * 函数: sc_ep_call_notify
 * 功能: CURL回调函数，保存数据，以便后续处理
 * 参数:
 *    void *pszBffer, S32 lSize, S32 lBlock, void *pArg
 * 返回值:
 *    成功返回参数lBlock， 否则返回0
 */
static S32 sc_ep_agent_update_recv(void *pszBffer, S32 lSize, S32 lBlock, void *pArg)
{
    IO_BUF_CB *pstIOBuffer = NULL;

    if (DOS_ADDR_INVALID(pArg))
    {
        DOS_ASSERT(0);
        return 0;
    }

    pstIOBuffer = (IO_BUF_CB *)pArg;

    if (dos_iobuf_append(pstIOBuffer, pszBffer, (U32)(lSize * lBlock)) != DOS_SUCC)
    {
        DOS_ASSERT(0);
        return 0;
    }

    return lBlock;
}


/* 解析一下格式的字符串
   : {"channel": "5_10000001_1001", "published_messages": "0", "stored_messages": "0", "subscribers": "1"}
   同时更新坐席状态 */
U32 sc_ep_update_agent_status(S8 *pszJSONString)
{
    JSON_OBJ_ST *pstJsonArrayItem     = NULL;
    const S8    *pszAgentID           = NULL;
    S8          szJobNum[16]          = { 0 };
    S8          szExtension[16]       = { 0 };
    U32         ulID;

    if (DOS_ADDR_INVALID(pszJSONString))
    {
        DOS_ASSERT(0);
        return DOS_FAIL;
    }

    pstJsonArrayItem = json_init(pszJSONString);
    if (DOS_ADDR_INVALID(pstJsonArrayItem))
    {
        DOS_ASSERT(0);
        return DOS_FAIL;
    }

    /* 更新坐席状态 */
    pszAgentID = json_get_param(pstJsonArrayItem, "channel");
    if (DOS_ADDR_INVALID(pszAgentID))
    {
        DOS_ASSERT(0);

        json_deinit(&pstJsonArrayItem);
        return DOS_FAIL;
    }

    if (dos_sscanf(pszAgentID, "%u_%[^_]_%s", &ulID, szJobNum, szExtension) != 3)
    {
        DOS_ASSERT(0);

        json_deinit(&pstJsonArrayItem);
        return DOS_FAIL;
    }

    json_deinit(&pstJsonArrayItem);

    return sc_acd_update_agent_status(SC_ACD_SITE_ACTION_ONLINE, ulID);
}

/* 解析这样一个json字符串
{"hostname": "localhost.localdomain"
    , "time": "2015-06-10T12:23:18"
    , "channels": "1"
    , "wildcard_channels": "0"
    , "uptime": "120366"
    , "infos": [{"channel": "5_10000001_1001", "published_messages": "0", "stored_messages": "0", "subscribers": "1"}
                , {"channel": "6_10000002_1002", "published_messages": "0", "stored_messages": "0", "subscribers": "1"}]}*/
U32 sc_ep_update_agent_req_proc(S8 *pszJSONString)
{
    S32 lIndex;
    S8 *pszAgentInfos = NULL;
    const S8 *pszAgentItem = NULL;
    JSON_OBJ_ST *pstJSONObj = NULL;
    JSON_OBJ_ST *pstJSONAgentInfos = NULL;

    pstJSONObj = json_init(pszJSONString);
    if (DOS_ADDR_INVALID(pstJSONObj))
    {
        DOS_ASSERT(0);

        sc_logr_notice(SC_ESL, "%s", "Update the agent FAIL while init the json string.");

        goto process_fail;
    }

    pszAgentInfos = (S8 *)json_get_param(pstJSONObj, "infos");
    if (DOS_ADDR_INVALID(pstJSONObj))
    {
        DOS_ASSERT(0);

        sc_logr_notice(SC_ESL, "%s", "Update the agent FAIL while get agent infos from the json string.");

        goto process_fail;
    }

    pstJSONAgentInfos = json_init(pszAgentInfos);
    if (DOS_ADDR_INVALID(pstJSONObj))
    {
        DOS_ASSERT(0);

        sc_logr_notice(SC_ESL, "%s", "Update the agent FAIL while get agent infos json array from the json string.");

        goto process_fail;
    }

    JSON_ARRAY_SCAN(lIndex, pstJSONAgentInfos, pszAgentItem)
    {
        sc_ep_update_agent_status((S8 *)pszAgentItem);
    }

    if (DOS_ADDR_INVALID(pstJSONObj))
    {
        json_deinit(&pstJSONObj);
    }

    if (DOS_ADDR_INVALID(pstJSONAgentInfos))
    {
        json_deinit(&pstJSONAgentInfos);
    }

    return DOS_SUCC;

process_fail:

    if (DOS_ADDR_INVALID(pstJSONObj))
    {
        json_deinit(&pstJSONObj);
    }

    if (DOS_ADDR_INVALID(pstJSONAgentInfos))
    {
        json_deinit(&pstJSONAgentInfos);
    }

    return DOS_FAIL;

}

U32 sc_ep_query_agent_status(SC_ACD_AGENT_INFO_ST *pstAgentInfo)
{
    S8 szURL[256] = { 0, };
    U32 ulRet = 0;
    IO_BUF_CB stIOBuffer = IO_BUF_INIT;

    if (DOS_ADDR_INVALID(g_pstCurlHandle))
    {
        g_pstCurlHandle = curl_easy_init();
        if (DOS_ADDR_INVALID(g_pstCurlHandle))
        {
            DOS_ASSERT(0);

            return DOS_FAIL;
        }
    }

    if (DOS_ADDR_INVALID(pstAgentInfo))
    {
        DOS_ASSERT(0);

        return DOS_FAIL;
    }

    dos_snprintf(szURL, sizeof(szURL)
                , "http://localhost/channels-stats?id=%u_%s_%s"
                , pstAgentInfo->ulSiteID
                , pstAgentInfo->szEmpNo
                , pstAgentInfo->szExtension);

    curl_easy_reset(g_pstCurlHandle);
    curl_easy_setopt(g_pstCurlHandle, CURLOPT_URL, szURL);
    curl_easy_setopt(g_pstCurlHandle, CURLOPT_WRITEFUNCTION, sc_ep_agent_update_recv);
    curl_easy_setopt(g_pstCurlHandle, CURLOPT_WRITEDATA, (VOID *)&stIOBuffer);
    ulRet = curl_easy_perform(g_pstCurlHandle);
    if(CURLE_OK != ulRet)
    {
        sc_logr_notice(SC_ESL, "%s, (%s)", "CURL get agent status FAIL.", curl_easy_strerror(ulRet));

        dos_iobuf_free(&stIOBuffer);
        return DOS_FAIL;
    }

    if (DOS_ADDR_INVALID(stIOBuffer.pszBuffer)
        || '\0' == stIOBuffer.pszBuffer)
    {
        DOS_ASSERT(0);
        ulRet = DOS_FAIL;
    }
    else
    {
        ulRet = DOS_FAIL;
    }

    dos_iobuf_free(&stIOBuffer);
    return ulRet;

}

U32 sc_ep_init_agent_status()
{
    S8 szURL[256] = { 0, };
    U32 ulRet = 0;
    IO_BUF_CB stIOBuffer = IO_BUF_INIT;

    if (DOS_ADDR_INVALID(g_pstCurlHandle))
    {
        g_pstCurlHandle = curl_easy_init();
        if (DOS_ADDR_INVALID(g_pstCurlHandle))
        {
            DOS_ASSERT(0);

            return DOS_FAIL;
        }
    }

    dos_snprintf(szURL, sizeof(szURL), "http://localhost/channels-stats?id=*");

    curl_easy_reset(g_pstCurlHandle);
    curl_easy_setopt(g_pstCurlHandle, CURLOPT_URL, szURL);
    curl_easy_setopt(g_pstCurlHandle, CURLOPT_WRITEFUNCTION, sc_ep_agent_update_recv);
    curl_easy_setopt(g_pstCurlHandle, CURLOPT_WRITEDATA, (VOID *)&stIOBuffer);
    ulRet = curl_easy_perform(g_pstCurlHandle);
    if(CURLE_OK != ulRet)
    {
        sc_logr_notice(SC_ESL, "%s, (%s)", "CURL get agent status FAIL.", curl_easy_strerror(ulRet));

        dos_iobuf_free(&stIOBuffer);
        return DOS_FAIL;
    }

    sc_logr_notice(SC_ESL, "%s", "CURL get agent status SUCC.Result");

    ulRet = sc_ep_update_agent_req_proc((S8 *)stIOBuffer.pszBuffer);

    dos_iobuf_free(&stIOBuffer);
    return ulRet;
}

BOOL sc_ep_black_regular_check(S8 *szRegularNum, S8 *szNum)
{
    S8 aszRegular[SC_TEL_NUMBER_LENGTH][SC_SINGLE_NUMBER_SRT_LEN] = {{0}};
    S8 szAllNum[SC_SINGLE_NUMBER_SRT_LEN] = "0123456789";
    S32 lIndex = 0;
    U32 ulLen  = 0;
    S8 *pszPos = szRegularNum;
    S8 ucChar;
    S32 i = 0;

    if (DOS_ADDR_INVALID(szRegularNum) || DOS_ADDR_INVALID(szNum))
    {
        /* 按照匹配不成功处理 */
        return DOS_TRUE;
    }

    while (*pszPos != '\0' && lIndex < SC_TEL_NUMBER_LENGTH)
    {
        ucChar = *pszPos;

        if (ucChar == '*')
        {
            dos_strcpy(aszRegular[lIndex], szAllNum);
        }
        else if (dos_strchr(szAllNum, ucChar) != NULL)
        {
            /* 0-9 */
            aszRegular[lIndex][0] = ucChar;
            aszRegular[lIndex][1] = '\0';
        }
        else if (ucChar == '[')
        {
            aszRegular[lIndex][0] = '\0';
            pszPos++;
            while (*pszPos != ']' && *pszPos != '\0')
            {
                if (dos_strchr(szAllNum, *pszPos) != NULL)
                {
                    /* 0-9, 先判断一下是否已经存在 */
                    if (dos_strchr(aszRegular[lIndex], *pszPos) == NULL)
                    {
                        ulLen = dos_strlen(aszRegular[lIndex]);
                        aszRegular[lIndex][ulLen] = *pszPos;
                        aszRegular[lIndex][ulLen+1] = '\0';
                    }
                }
                pszPos++;
            }

            if (*pszPos == '\0')
            {
                /* 正则表达式错误, 按照不匹配来处理 */
                return DOS_TRUE;
            }
        }
        else
        {
            /* 正则表达式错误, 按照不匹配来处理 */
            return DOS_TRUE;
        }

        pszPos++;
        lIndex++;
    }

    /* 正则表达式解析完成，比较号码是否满足正则表达式。首先比较长度，lIndex即为正则表达式的长度 */
    if (dos_strlen(szNum) != lIndex)
    {
        return DOS_TRUE;
    }

    for (i=0; i<lIndex; i++)
    {
        if (dos_strchr(aszRegular[i], *(szNum+i)) == NULL)
        {
            /* 不匹配 */
            return DOS_TRUE;
        }
    }

    return DOS_FALSE;
}

/**
 * 函数: BOOL sc_ep_black_list_check(U32 ulCustomerID, S8 *pszNum)
 * 功能: 检查pszNum是否被黑名单过滤
 * 参数:
 *       U32 ulCustomerID : 客户编号，如果是非法值，将会只对全局黑名单过滤
 *       S8 *pszNum       : 需要检查的号码
 * 返回值: 如果pszNum被黑名单过滤，将返回DOS_FALSE，否则返回TRUE
 */
BOOL sc_ep_black_list_check(U32 ulCustomerID, S8 *pszNum)
{
    U32                ulHashIndex;
    HASH_NODE_S        *pstHashNode = NULL;
    SC_BLACK_LIST_NODE *pstBlackListNode = NULL;


    if (DOS_ADDR_INVALID(pszNum))
    {
        DOS_ASSERT(0);
        return DOS_FALSE;
    }

    sc_logr_debug(SC_ESL, "Check num %s is in black list for customer %u", pszNum, ulCustomerID);

    pthread_mutex_lock(&g_mutexHashBlackList);
    HASH_Scan_Table(g_pstHashBlackList, ulHashIndex)
    {
        HASH_Scan_Bucket(g_pstHashBlackList, ulHashIndex, pstHashNode, HASH_NODE_S *)
        {
            if (DOS_ADDR_INVALID(pstHashNode))
            {
                DOS_ASSERT(0);
                break;
            }

            pstBlackListNode = pstHashNode->pHandle;
            if (DOS_ADDR_INVALID(pstBlackListNode))
            {
                DOS_ASSERT(0);
                continue;
            }

            if (SC_TOP_USER_ID != pstBlackListNode->ulCustomerID
                && ulCustomerID != pstBlackListNode->ulCustomerID)
            {
                continue;
            }

            if (SC_NUM_BLACK_REGULAR == pstBlackListNode->enType)
            {
                /* 正则号码 */
                if (sc_ep_black_regular_check(pstBlackListNode->szNum, pszNum) == DOS_FALSE)
                {
                    /* 匹配成功 */
                    sc_logr_debug(SC_ESL, "Num %s is matched black list item %s, id %u. (Customer:%u)"
                                , pszNum
                                , pstBlackListNode->szNum
                                , pstBlackListNode->ulID
                                , ulCustomerID);

                    pthread_mutex_unlock(&g_mutexHashBlackList);

                    return DOS_FALSE;
                }
            }
            else
            {
                if (0 == dos_strcmp(pstBlackListNode->szNum, pszNum))
                {
                    sc_logr_debug(SC_ESL, "Num %s is matched black list item %s, id %u. (Customer:%u)"
                                , pszNum
                                , pstBlackListNode->szNum
                                , pstBlackListNode->ulID
                                , ulCustomerID);

                    pthread_mutex_unlock(&g_mutexHashBlackList);
                    return DOS_FALSE;
                }
            }
        }
    }
    pthread_mutex_unlock(&g_mutexHashBlackList);

    sc_logr_debug(SC_ESL, "Num %s is not matched any black list. (Customer:%u)", pszNum, ulCustomerID);
    return DOS_TRUE;
}

/**
 * 函数: VOID sc_ep_sip_userid_init(SC_USER_ID_NODE_ST *pstUserID)
 * 功能: 初始化pstUserID所指向的User ID描述结构
 * 参数:
 *      SC_USER_ID_NODE_ST *pstUserID 需要别初始化的结构
 * 返回值: VOID
 */
VOID sc_ep_sip_userid_init(SC_USER_ID_NODE_ST *pstUserID)
{
    if (pstUserID)
    {
        dos_memzero(pstUserID, sizeof(SC_USER_ID_NODE_ST));

        pstUserID->ulCustomID = U32_BUTT;
        pstUserID->ulSIPID = U32_BUTT;
    }
}

/**
 * 函数: VOID sc_ep_did_init(SC_DID_NODE_ST *pstDIDNum)
 * 功能: 初始化pstDIDNum所指向的DID号码描述结构
 * 参数:
 *      SC_DID_NODE_ST *pstDIDNum 需要别初始化的DID号码结构
 * 返回值: VOID
 */
VOID sc_ep_did_init(SC_DID_NODE_ST *pstDIDNum)
{
    if (pstDIDNum)
    {
        dos_memzero(pstDIDNum, sizeof(SC_DID_NODE_ST));
        pstDIDNum->ulBindID = U32_BUTT;
        pstDIDNum->ulBindType = U32_BUTT;
        pstDIDNum->ulCustomID = U32_BUTT;
        pstDIDNum->ulDIDID = U32_BUTT;
        pstDIDNum->ulTimes = 0;
    }
}

VOID sc_ep_tt_init(SC_TT_NODE_ST *pstTTNumber)
{
    if (pstTTNumber)
    {
        pstTTNumber->ulID = U32_BUTT;
        pstTTNumber->szAddr[0] = '\0';
        pstTTNumber->szPrefix[0] = '\10';
    }
}

/**
 * 函数: VOID sc_ep_route_init(SC_ROUTE_NODE_ST *pstRoute)
 * 功能: 初始化pstRoute所指向的路由描述结构
 * 参数:
 *      SC_ROUTE_NODE_ST *pstRoute 需要别初始化的路由描述结构
 * 返回值: VOID
 */
VOID sc_ep_route_init(SC_ROUTE_NODE_ST *pstRoute)
{
    if (pstRoute)
    {
        dos_memzero(pstRoute, sizeof(SC_ROUTE_NODE_ST));
        pstRoute->ulID = U32_BUTT;

        pstRoute->ucHourBegin = 0;
        pstRoute->ucMinuteBegin = 0;
        pstRoute->ucHourEnd = 0;
        pstRoute->ucMinuteEnd = 0;
        pstRoute->bExist = DOS_FALSE;
        pstRoute->bStatus = DOS_FALSE;
    }
}

VOID sc_ep_num_transform_init(SC_NUM_TRANSFORM_NODE_ST *pstNumTransform)
{
    if (pstNumTransform)
    {
        dos_memzero(pstNumTransform, sizeof(SC_NUM_TRANSFORM_NODE_ST));
        pstNumTransform->ulID = U32_BUTT;
        pstNumTransform->bExist = DOS_FALSE;
    }
}

VOID sc_ep_customer_init(SC_CUSTOMER_NODE_ST *pstCustomer)
{
    if (pstCustomer)
    {
        dos_memzero(pstCustomer, sizeof(SC_CUSTOMER_NODE_ST));
        pstCustomer->ulID = U32_BUTT;
        pstCustomer->bExist = DOS_FALSE;
    }
}


/**
 * 函数: VOID sc_ep_caller_init(SC_CALLER_QUERY_NODE_ST  *pstCaller)
 * 功能: 初始化pstCaller所指向的主叫号码描述结构
 * 参数:
 *      SC_CALLER_QUERY_NODE_ST  *pstCaller 需要别初始化的主叫号码描述结构
 * 返回值: VOID
 */
VOID sc_ep_caller_init(SC_CALLER_QUERY_NODE_ST  *pstCaller)
{
    if (pstCaller)
    {
        dos_memzero(pstCaller, sizeof(SC_CALLER_QUERY_NODE_ST));
        /* 默认关系跟踪 */
        pstCaller->bTraceON = DOS_FALSE;
        /* 默认可用 */
        pstCaller->bValid = DOS_TRUE;
        pstCaller->ulCustomerID = U32_BUTT;
        pstCaller->ulIndexInDB = U32_BUTT;
        /* 号码被命中次数初始化为0 */
        pstCaller->ulTimes = 0;

        dos_memzero(pstCaller->szNumber, sizeof(pstCaller->szNumber));
    }
}

/**
 * 函数: VOID sc_ep_caller_grp_init(SC_CALLER_GRP_NODE_ST* pstCallerGrp)
 * 功能: 初始化pstCallerGrp所指向的主叫号码组描述结构
 * 参数:
 *      SC_CALLER_GRP_NODE_ST* pstCallerGrp 需要别初始化的主叫号码组描述结构
 * 返回值: VOID
 */
VOID sc_ep_caller_grp_init(SC_CALLER_GRP_NODE_ST* pstCallerGrp)
{
    if (pstCallerGrp)
    {
        dos_memzero(pstCallerGrp, sizeof(SC_CALLER_GRP_NODE_ST));

        /* 初始化为非默认组 */
        pstCallerGrp->bDefault = DOS_FALSE;

        pstCallerGrp->ulID = U32_BUTT;
        pstCallerGrp->ulCustomerID = U32_BUTT;
        /* 暂时保留现状 */
        pstCallerGrp->ulLastNo = 0;
        /* 默认使用顺序呼叫策略 */
        pstCallerGrp->ulPolicy = SC_CALLER_SELECT_POLICY_IN_ORDER;
        dos_memzero(pstCallerGrp->szGrpName, sizeof(pstCallerGrp->szGrpName));
        DLL_Init(&pstCallerGrp->stCallerList);
    }
}

VOID sc_ep_caller_setting_init(SC_CALLER_SETTING_ST *pstCallerSetting)
{
    if (pstCallerSetting)
    {
        dos_memzero(pstCallerSetting, sizeof(SC_CALLER_SETTING_ST));
        pstCallerSetting->ulID = U32_BUTT;
        pstCallerSetting->ulSrcID = U32_BUTT;
        pstCallerSetting->ulSrcType = U32_BUTT;
        pstCallerSetting->ulDstID = U32_BUTT;
        pstCallerSetting->ulDstType = U32_BUTT;
        pstCallerSetting->ulCustomerID = U32_BUTT;
    }
}

/**
 * 函数: VOID sc_ep_gw_init(SC_GW_NODE_ST *pstGW)
 * 功能: 初始化pstGW所指向的网关描述结构
 * 参数:
 *      SC_GW_NODE_ST *pstGW 需要别初始化的网关描述结构
 * 返回值: VOID
 */
VOID sc_ep_gw_init(SC_GW_NODE_ST *pstGW)
{
    if (pstGW)
    {
        dos_memzero(pstGW, sizeof(SC_GW_NODE_ST));
        pstGW->ulGWID = U32_BUTT;
        pstGW->bExist = DOS_FALSE;
        pstGW->bStatus = DOS_FALSE;
    }
}

/**
 * 函数: VOID sc_ep_gw_init(SC_GW_NODE_ST *pstGW)
 * 功能: 初始化pstGW所指向的网关描述结构
 * 参数:
 *      SC_GW_NODE_ST *pstGW 需要别初始化的网关描述结构
 * 返回值: VOID
 */
VOID sc_ep_black_init(SC_BLACK_LIST_NODE *pstBlackListNode)
{
    if (pstBlackListNode)
    {
        dos_memzero(pstBlackListNode, sizeof(SC_BLACK_LIST_NODE));
        pstBlackListNode->ulID = U32_BUTT;
        pstBlackListNode->ulCustomerID = U32_BUTT;
        pstBlackListNode->enType = SC_NUM_BLACK_BUTT;
        pstBlackListNode->szNum[0] = '\0';
    }
}


/* 网关组的hash函数 */
U32 sc_ep_gw_grp_hash_func(U32 ulGWGrpID)
{
    return ulGWGrpID % SC_GW_GRP_HASH_SIZE;
}

/* 网关的hash函数 */
U32 sc_ep_gw_hash_func(U32 ulGWID)
{
    return ulGWID % SC_GW_GRP_HASH_SIZE;
}

U32 sc_ep_tt_hash_func(U32 ulID)
{
    return ulID % SC_TT_NUMBER_HASH_SIZE;
}

/**
 * 函数: U32 sc_ep_caller_hash_func(U32 ulCallerID)
 * 功能: 通过ulCallerID计算主叫号码id的哈希值
 * 参数:
 *      U32 ulCallerID 主叫号码id
 * 返回值: U32 返回hash值
 */
U32 sc_ep_caller_hash_func(U32 ulCallerID)
{
    return ulCallerID % SC_CALLER_HASH_SIZE;
}

/**
 * 函数: U32 sc_ep_caller_grp_hash_func(U32 ulGrpID)
 * 功能: 通过ulGrpID计算主叫号码组id的哈希值
 * 参数:
 *      U32 ulGrpID 主叫号码组id
 * 返回值: U32 返回hash值
 */
U32 sc_ep_caller_grp_hash_func(U32 ulGrpID)
{
    return ulGrpID % SC_CALLER_GRP_HASH_SIZE;
}

U32 sc_ep_caller_setting_hash_func(U32 ulSettingID)
{
    return ulSettingID % SC_CALLER_SETTING_HASH_SIZE;
}

/**
 * 函数: U32 sc_ep_number_lmt_hash_func(U32 ulCustomerID)
 * 功能: 根据号码pszNumber计算号码限制hash节点的hash值
 * 参数:
 *      U32 pszNumber 号码
 * 返回值: U32 返回hash值
 */
U32 sc_ep_number_lmt_hash_func(S8 *pszNumber)
{
    U32 ulIndex = 0;
    U32 ulHashIndex = 0;

    ulIndex = 0;
    for (;;)
    {
        if ('\0' == pszNumber[ulIndex])
        {
            break;
        }

        ulHashIndex += (pszNumber[ulIndex] << 3);

        ulIndex++;
    }

    return ulHashIndex % SC_NUMBER_LMT_HASH_SIZE;

}


/**
 * 函数: static U32 sc_sip_userid_hash_func(S8 *pszUserID)
 * 功能: 通过pszUserID计算SIP User IDHash表的HASH值
 * 参数:
 *      S8 *pszUserID 当前HASH节点的User ID
 * 返回值: U32 返回hash值
 */
static U32 sc_sip_userid_hash_func(S8 *pszUserID)
{
    U32 ulIndex = 0;
    U32 ulHashIndex = 0;

    ulIndex = 0;
    for (;;)
    {
        if ('\0' == pszUserID[ulIndex])
        {
            break;
        }

        ulHashIndex += (pszUserID[ulIndex] << 3);

        ulIndex++;
    }

    return ulHashIndex % SC_IP_USERID_HASH_SIZE;
}

/**
 * 函数: static U32 sc_sip_did_hash_func(S8 *pszDIDNum)
 * 功能: 通过pszDIDNum计算DID号码Hash表的HASH值
 * 参数:
 *      S8 *pszDIDNum 当前HASH节点的DID号码
 * 返回值: U32 返回hash值
 */
static U32 sc_sip_did_hash_func(S8 *pszDIDNum)
{
    U32 ulIndex = 0;
    U32 ulHashIndex = 0;

    ulIndex = 0;
    for
(;;)
    {
        if ('\0' == pszDIDNum[ulIndex])
        {
            break;
        }

        ulHashIndex += (pszDIDNum[ulIndex] << 3);

        ulIndex++;
    }

    return ulHashIndex % SC_IP_DID_HASH_SIZE;
}



/**
 * 函数: static U32 sc_black_list_hash_func(S8 *pszNum)
 * 功能: 计算黑名单hash节点的hash值
 * 参数:
 *      S8 *pszNum : 当前黑名单号码
 * 返回值: U32 返回hash值
 */
static U32 sc_ep_black_list_hash_func(U32 ulID)
{
    return ulID % SC_IP_USERID_HASH_SIZE;
}

S32 sc_ep_caller_hash_find(VOID *pObj, HASH_NODE_S *pstHashNode)
{
    SC_CALLER_QUERY_NODE_ST *pstCaller = NULL;
    U32 ulIndex = U32_BUTT;

    if (DOS_ADDR_INVALID(pObj)
        || DOS_ADDR_INVALID(pstHashNode)
        || DOS_ADDR_INVALID(pstHashNode->pHandle))
    {
        DOS_ASSERT(0);
        return DOS_FAIL;
    }

    ulIndex = *(U32 *)pObj;
    pstCaller = (SC_CALLER_QUERY_NODE_ST *)pstHashNode->pHandle;

    if (ulIndex == pstCaller->ulIndexInDB)
    {
        return DOS_SUCC;
    }
    else
    {
        return DOS_FAIL;
    }
}

/**
 * 函数: S32 sc_ep_caller_grp_hash_find(VOID *pObj, HASH_NODE_S *pstHashNode)
 * 功能: 查找关键字,主叫号码组查找函数
 * 参数:
 *      VOID *pObj, : 查找关键字，这里是号码组客户id
 *      HASH_NODE_S *pstHashNode  当前哈希
 * 返回值: 查找成功返回DOS_SUCC，否则返回DOS_FAIL.
 */
S32 sc_ep_caller_grp_hash_find(VOID *pObj, HASH_NODE_S *pstHashNode)
{
    SC_CALLER_GRP_NODE_ST *pstCallerGrp = NULL;
    U32  ulCustomerID = U32_BUTT;

    if (DOS_ADDR_INVALID(pObj)
            || DOS_ADDR_INVALID(pstHashNode)
            || DOS_ADDR_INVALID(pstHashNode->pHandle))
    {
        DOS_ASSERT(0);
        return DOS_FAIL;
    }

    ulCustomerID = *(U32 *)pObj;
    pstCallerGrp = pstHashNode->pHandle;

    if (ulCustomerID == pstCallerGrp->ulID)
    {
        return DOS_SUCC;
    }
    else
    {
        return DOS_FAIL;
    }
}

S32 sc_ep_caller_setting_hash_find(VOID *pObj, HASH_NODE_S *pstHashNode)
{
    SC_CALLER_SETTING_ST      *pstSetting = NULL;
    U32  ulCustomerID = U32_BUTT;

    if (DOS_ADDR_INVALID(pObj)
            || DOS_ADDR_INVALID(pstHashNode)
            || DOS_ADDR_INVALID(pstHashNode->pHandle))
    {
        DOS_ASSERT(0);
        return DOS_FAIL;
    }
    ulCustomerID = *(U32 *)pObj;
    pstSetting = (SC_CALLER_SETTING_ST *)pstHashNode->pHandle;

    if (ulCustomerID == pstSetting->ulID)
    {
        return DOS_SUCC;
    }
    else
    {
        return DOS_FAIL;
    }
}

/* 网关组hash表查找函数 */
S32 sc_ep_gw_grp_hash_find(VOID *pObj, HASH_NODE_S *pstHashNode)
{
    SC_GW_GRP_NODE_ST *pstGWGrpNode;

    U32 ulGWGrpIndex = 0;

    if (DOS_ADDR_INVALID(pObj)
        || DOS_ADDR_INVALID(pstHashNode)
        || DOS_ADDR_INVALID(pstHashNode->pHandle))
    {
        DOS_ASSERT(0);

        return DOS_FAIL;
    }

    ulGWGrpIndex = *(U32 *)pObj;
    pstGWGrpNode = pstHashNode->pHandle;

    if (ulGWGrpIndex == pstGWGrpNode->ulGWGrpID)
    {
        return DOS_SUCC;
    }
    else
    {
        return DOS_FAIL;
    }
}

/* 网关hash表查找函数 */
S32 sc_ep_gw_hash_find(VOID *pObj, HASH_NODE_S *pstHashNode)
{
    SC_GW_NODE_ST *pstGWNode;

    U32 ulGWIndex = 0;

    if (DOS_ADDR_INVALID(pObj)
        || DOS_ADDR_INVALID(pstHashNode)
        || DOS_ADDR_INVALID(pstHashNode->pHandle))
    {
        DOS_ASSERT(0);

        return DOS_FAIL;
    }

    ulGWIndex = *(U32 *)pObj;
    pstGWNode = pstHashNode->pHandle;

    if (ulGWIndex == pstGWNode->ulGWID)
    {
        return DOS_SUCC;
    }
    else
    {
        return DOS_FAIL;
    }

}


/* 查找DID号码 */
S32 sc_ep_did_hash_find(VOID *pObj, HASH_NODE_S *pstHashNode)
{
    S8 *pszDIDNum = NULL;
    SC_DID_NODE_ST *pstDIDInfo = NULL;

    if (DOS_ADDR_INVALID(pObj)
        || DOS_ADDR_INVALID(pstHashNode)
        || DOS_ADDR_INVALID(pstHashNode->pHandle))
    {
        DOS_ASSERT(0);

        return DOS_FAIL;
    }

    pszDIDNum = (S8 *)pObj;
    pstDIDInfo = pstHashNode->pHandle;

    if (dos_strnicmp(pstDIDInfo->szDIDNum, pszDIDNum, sizeof(pstDIDInfo->szDIDNum)))
    {
        return DOS_FAIL;
    }

    return DOS_SUCC;
}

/* 查找SIP User ID */
S32 sc_ep_sip_userid_hash_find(VOID *pObj, HASH_NODE_S *pstHashNode)
{
    S8 *pszDIDNum = NULL;
    SC_USER_ID_NODE_ST *pstSIPInfo = NULL;

    if (DOS_ADDR_INVALID(pObj)
        || DOS_ADDR_INVALID(pstHashNode)
        || DOS_ADDR_INVALID(pstHashNode->pHandle))
    {
        DOS_ASSERT(0);

        return DOS_FAIL;
    }

    pszDIDNum = (S8 *)pObj;
    pstSIPInfo = pstHashNode->pHandle;

    if (dos_strnicmp(pstSIPInfo->szUserID, pszDIDNum, sizeof(pstSIPInfo->szUserID)))
    {
        return DOS_FAIL;
    }

    return DOS_SUCC;
}

/* 查找路由 */
S32 sc_ep_route_find(VOID *pKey, DLL_NODE_S *pstDLLNode)
{
    SC_ROUTE_NODE_ST *pstRouteCurrent;
    U32 ulIndex;

    if (DOS_ADDR_INVALID(pKey)
        || DOS_ADDR_INVALID(pstDLLNode)
        || DOS_ADDR_INVALID(pstDLLNode->pHandle))
    {
        return DOS_FAIL;
    }

    ulIndex = *(U32 *)pKey;
    pstRouteCurrent = pstDLLNode->pHandle;

    if (ulIndex == pstRouteCurrent->ulID)
    {
        return DOS_SUCC;
    }

    return DOS_FAIL;
}

S32 sc_ep_num_transform_find(VOID *pKey, DLL_NODE_S *pstDLLNode)
{
    SC_NUM_TRANSFORM_NODE_ST *pstTransformCurrent;
    U32 ulIndex;

    if (DOS_ADDR_INVALID(pKey)
        || DOS_ADDR_INVALID(pstDLLNode)
        || DOS_ADDR_INVALID(pstDLLNode->pHandle))
    {
        return DOS_FAIL;
    }

    ulIndex = *(U32 *)pKey;
    pstTransformCurrent = pstDLLNode->pHandle;

    if (ulIndex == pstTransformCurrent->ulID)
    {
        return DOS_SUCC;
    }

    return DOS_FAIL;
}

S32 sc_ep_customer_find(VOID *pKey, DLL_NODE_S *pstDLLNode)
{
    SC_CUSTOMER_NODE_ST *pstCustomerCurrent;
    U32 ulIndex;

    if (DOS_ADDR_INVALID(pKey)
        || DOS_ADDR_INVALID(pstDLLNode)
        || DOS_ADDR_INVALID(pstDLLNode->pHandle))
    {
        return DOS_FAIL;
    }

    ulIndex = *(U32 *)pKey;
    pstCustomerCurrent = pstDLLNode->pHandle;

    if (ulIndex == pstCustomerCurrent->ulID)
    {
        return DOS_SUCC;
    }

    return DOS_FAIL;
}

/* 查找黑名单 */
S32 sc_ep_black_list_find(VOID *pObj, HASH_NODE_S *pstHashNode)
{
    SC_BLACK_LIST_NODE *pstBlackList = NULL;
    U32  ulID;

    if (DOS_ADDR_INVALID(pObj)
        || DOS_ADDR_INVALID(pstHashNode)
        || DOS_ADDR_INVALID(pstHashNode->pHandle))
    {
        return DOS_FAIL;
    }

    ulID = *(U32 *)pObj;
    pstBlackList = pstHashNode->pHandle;

    if (ulID == pstBlackList->ulID)
    {
        return DOS_SUCC;
    }


    return DOS_FAIL;
}

S32 sc_ep_tt_list_find(VOID *pObj, HASH_NODE_S *pstHashNode)
{
    SC_TT_NODE_ST *pstTTNumber = NULL;
    U32  ulID;

    if (DOS_ADDR_INVALID(pObj)
        || DOS_ADDR_INVALID(pstHashNode)
        || DOS_ADDR_INVALID(pstHashNode->pHandle))
    {
        return DOS_FAIL;
    }

    ulID = *(U32 *)pObj;
    pstTTNumber = pstHashNode->pHandle;

    if (ulID == pstTTNumber->ulID)
    {
        return DOS_SUCC;
    }

    return DOS_FAIL;
}

S32 sc_ep_number_lmt_find(VOID *pObj, HASH_NODE_S *pstHashNode)
{
    SC_NUMBER_LMT_NODE_ST *pstNumber = NULL;
    S8 *pszNumber = (S8 *)pObj;

    if (DOS_ADDR_INVALID(pObj)
        || DOS_ADDR_INVALID(pstHashNode)
        || DOS_ADDR_INVALID(pstHashNode->pHandle))
    {
        return DOS_FAIL;
    }

    pstNumber = pstHashNode->pHandle;

    if (0 == dos_strcmp(pstNumber->szPrefix, pszNumber))
    {
        return DOS_SUCC;
    }

    return DOS_FAIL;

}

/* 根据userid 更新状态 */
U32 sc_ep_update_sip_status(S8 *szUserID, SC_STATUS_TYPE_EN enStatus, U32 *pulSipID)
{
    SC_USER_ID_NODE_ST *pstUserID   = NULL;
    HASH_NODE_S        *pstHashNode = NULL;
    U32                ulHashIndex  = U32_BUTT;

    ulHashIndex= sc_sip_userid_hash_func(szUserID);
    pthread_mutex_lock(&g_mutexHashSIPUserID);
    pstHashNode = hash_find_node(g_pstHashSIPUserID, ulHashIndex, (VOID *)szUserID, sc_ep_sip_userid_hash_find);
    if (DOS_ADDR_INVALID(pstHashNode)
        || DOS_ADDR_INVALID(pstHashNode->pHandle))
    {
        pthread_mutex_unlock(&g_mutexHashSIPUserID);
        DOS_ASSERT(0);

        return DOS_FAIL;
    }

    pstUserID = pstHashNode->pHandle;
    if (DOS_ADDR_INVALID(pstUserID))
    {
        pthread_mutex_unlock(&g_mutexHashSIPUserID);
        DOS_ASSERT(0);

        return DOS_FAIL;
    }

    pstUserID->enStatus = enStatus;
    switch (enStatus)
    {
        case SC_STATUS_TYPE_REGISTER:
            pstUserID->stStat.ulRegisterCnt++;
            break;
        case SC_STATUS_TYPE_UNREGISTER:
            pstUserID->stStat.ulUnregisterCnt++;
            break;
        default:
            break;
    }

    *pulSipID = pstUserID->ulSIPID;
    pthread_mutex_unlock(&g_mutexHashSIPUserID);

    return DOS_SUCC;
}

/* 删除SIP账户 */
U32 sc_ep_sip_userid_delete(S8 * pszSipID)
{
    SC_USER_ID_NODE_ST *pstUserID   = NULL;
    HASH_NODE_S        *pstHashNode = NULL;
    U32                ulHashIndex  = U32_BUTT;

    ulHashIndex= sc_sip_userid_hash_func(pszSipID);
    pstHashNode = hash_find_node(g_pstHashSIPUserID, ulHashIndex, (VOID *)pszSipID, sc_ep_sip_userid_hash_find);
    if (DOS_ADDR_INVALID(pstHashNode)
        || DOS_ADDR_INVALID(pstHashNode->pHandle))
    {
        DOS_ASSERT(0);

        return DOS_FAIL;
    }

    hash_delete_node(g_pstHashSIPUserID, pstHashNode, ulHashIndex);
    pstUserID = pstHashNode->pHandle;
    HASH_Init_Node(pstHashNode);
    pstHashNode->pHandle = NULL;

    dos_dmem_free(pstUserID);
    pstUserID = NULL;
    dos_dmem_free(pstHashNode);
    pstHashNode = NULL;

    return DOS_SUCC;
}

U32 sc_caller_delete(U32 ulCallerID)
{
    HASH_NODE_S *pstHashNode = NULL;
    SC_CALLER_QUERY_NODE_ST *pstCaller = NULL;
    U32  ulHashIndex = U32_BUTT;
    BOOL bFound = DOS_FALSE;
#if 1
    S32 lIndex;
    SC_TASK_CB_ST *pstTaskCB = NULL;
#endif

    HASH_Scan_Table(g_pstHashCaller, ulHashIndex)
    {
        HASH_Scan_Bucket(g_pstHashCaller, ulHashIndex, pstHashNode, HASH_NODE_S *)
        {
            if (DOS_ADDR_INVALID(pstHashNode)
                || DOS_ADDR_INVALID(pstHashNode->pHandle))
            {
                continue;
            }

            pstCaller = (SC_CALLER_QUERY_NODE_ST *)pstHashNode->pHandle;
            if (ulCallerID == pstCaller->ulIndexInDB)
            {
                bFound = DOS_TRUE;
                break;
            }
        }
        if (DOS_TRUE == bFound)
        {
            break;
        }
    }
#if 1
    /* 现在号码组的主叫号码与群呼任务的主叫号码是两份数据，同步起来比较恼火，将来解决了两份数据问题肯定要删掉
       目前先暂时将其状态置为invalid即可 */
    for (lIndex = 0; lIndex < SC_MAX_TASK_NUM; lIndex++)
    {
        pstTaskCB = &g_pstTaskMngtInfo->pstTaskList[lIndex];
        if (g_pstTaskMngtInfo->pstTaskList[lIndex].ulCustomID == pstCaller->ulCustomerID)
        {
            break;
        }
    }
    for (lIndex = 0; lIndex < SC_MAX_CALLER_NUM; lIndex++)
    {
        if (pstTaskCB->pstCallerNumQuery[lIndex].bValid
            && pstTaskCB->pstCallerNumQuery[lIndex].ulIndexInDB == pstCaller->ulIndexInDB
            && 0 == dos_strcmp(pstTaskCB->pstCallerNumQuery[lIndex].szNumber, pstCaller->szNumber))
        {
            pstTaskCB->pstCallerNumQuery[lIndex].bValid = DOS_FALSE;
            break;
        }
    }
#endif
    if (DOS_FALSE == bFound)
    {
        DOS_ASSERT(0);
        sc_logr_error(SC_FUNC, "Delete Caller FAIL.(ulID:%u)", ulCallerID);
        return DOS_FAIL;
    }
    else
    {
        hash_delete_node(g_pstHashCaller, pstHashNode, ulHashIndex);
        dos_dmem_free(pstCaller);
        pstCaller = NULL;

        dos_dmem_free(pstHashNode);
        pstHashNode = NULL;

        sc_logr_info(SC_FUNC, "Delete Caller SUCC.(ulID:%u)", ulCallerID);

        return DOS_SUCC;
    }
}

U32 sc_caller_grp_delete(U32 ulCallerGrpID)
{
    HASH_NODE_S *pstHashNode = NULL;
    DLL_NODE_S *pstNode = NULL;
    SC_CALLER_GRP_NODE_ST *pstCallerGrp = NULL;
    U32  ulHashIndex = U32_BUTT;
    BOOL bFound = DOS_FALSE;

    HASH_Scan_Table(g_pstHashCallerGrp,ulHashIndex)
    {
        HASH_Scan_Bucket(g_pstHashCallerGrp, ulHashIndex, pstHashNode, HASH_NODE_S *)
        {
            if (DOS_ADDR_INVALID(pstHashNode)
                || DOS_ADDR_INVALID(pstHashNode->pHandle))
            {
                continue;
            }

            pstCallerGrp = (SC_CALLER_GRP_NODE_ST *)pstHashNode->pHandle;
            if (ulCallerGrpID == pstCallerGrp->ulID)
            {
                bFound = DOS_TRUE;
                break;
            }
        }
        if (DOS_TRUE == bFound)
        {
            break;
        }
    }
    if (DOS_FALSE == bFound)
    {
        DOS_ASSERT(0);
        sc_logr_error(SC_FUNC, "Delete Caller Group FAIL.(ulID:%u)", ulCallerGrpID);
        return DOS_FAIL;
    }
    else
    {
        DLL_Scan(&pstCallerGrp->stCallerList, pstNode, DLL_NODE_S *)
        {
            if (DOS_ADDR_INVALID(pstNode)
                || DOS_ADDR_INVALID(pstNode->pHandle))
            {
                continue;
            }
            dll_delete(&pstCallerGrp->stCallerList, pstNode);
            dos_dmem_free(pstNode->pHandle);
            pstNode->pHandle = NULL;
            dos_dmem_free(pstNode);
            pstNode = NULL;
        }

        hash_delete_node(g_pstHashCallerGrp, pstHashNode, ulHashIndex);
        dos_dmem_free(pstCallerGrp);
        pstCallerGrp = NULL;

        dos_dmem_free(pstHashNode);
        pstHashNode = NULL;
        sc_logr_info(SC_FUNC, "Delete Caller Group SUCC.(ulID:%u)", ulCallerGrpID);

        return DOS_SUCC;
    }
}

U32 sc_caller_setting_delete(U32 ulSettingID)
{
    HASH_NODE_S  *pstHashNode = NULL;
    SC_CALLER_SETTING_ST *pstSetting = NULL;
    U32 ulHashIndex = U32_BUTT;
    BOOL bFound = DOS_FALSE;

    HASH_Scan_Table(g_pstHashCallerSetting, ulHashIndex)
    {
        HASH_Scan_Bucket(g_pstHashCallerSetting, ulHashIndex, pstHashNode, HASH_NODE_S *)
        {
            if (DOS_ADDR_INVALID(pstHashNode)
                            || DOS_ADDR_INVALID(pstHashNode->pHandle))
            {
                continue;
            }
            pstSetting = pstHashNode->pHandle;
            if (ulSettingID == pstSetting->ulID)
            {
                bFound = DOS_TRUE;
                break;
            }
        }
        if (DOS_FALSE == bFound)
        {
            DOS_ASSERT(0);
            sc_logr_error(SC_FUNC, "Delete Caller Setting FAIL.(ulID:%u)", ulSettingID);
            return DOS_FAIL;
        }
    }
    if (DOS_FALSE == bFound)
    {
        DOS_ASSERT(0);
        sc_logr_error(SC_FUNC, "Delete Caller Setting FAIL.(ulID:%u)", ulSettingID);
        return DOS_FAIL;
    }
    else
    {
        hash_delete_node(g_pstHashCallerSetting, pstHashNode, ulHashIndex);
        dos_dmem_free(pstSetting);
        pstSetting = NULL;

        dos_dmem_free(pstHashNode);
        pstHashNode = NULL;

        sc_logr_info(SC_FUNC, "Delete Caller Setting SUCC.(ulID:%u)", ulSettingID);

        return DOS_SUCC;
    }

    return DOS_SUCC;
}

U32 sc_transform_delete(U32 ulTransformID)
{
    DLL_NODE_S *pstListNode = NULL;

    pthread_mutex_lock(&g_mutexNumTransformList);
    pstListNode = dll_find(&g_stNumTransformList, &ulTransformID, sc_ep_num_transform_find);
    if (DOS_ADDR_INVALID(pstListNode)
        || DOS_ADDR_INVALID(pstListNode->pHandle))
    {
        pthread_mutex_unlock(&g_mutexNumTransformList);
        DOS_ASSERT(0);
        sc_logr_error(SC_FUNC, "Num Transform Delete FAIL.ID %u does not exist.", ulTransformID);
        return DOS_FAIL;
    }

    dll_delete(&g_stNumTransformList, pstListNode);
    pthread_mutex_unlock(&g_mutexNumTransformList);
    dos_dmem_free(pstListNode->pHandle);
    pstListNode->pHandle= NULL;
    dos_dmem_free(pstListNode);
    pstListNode = NULL;
    sc_logr_debug(SC_FUNC, "Delete Num Transform SUCC.(ID:%u)", ulTransformID);

    return DOS_SUCC;
}

U32 sc_customer_delete(U32 ulCustomerID)
{
    DLL_NODE_S *pstListNode = NULL;

    pthread_mutex_lock(&g_mutexCustomerList);
    pstListNode = dll_find(&g_stCustomerList, &ulCustomerID, sc_ep_customer_find);
    if (DOS_ADDR_INVALID(pstListNode)
        || DOS_ADDR_INVALID(pstListNode->pHandle))
    {
        pthread_mutex_unlock(&g_mutexCustomerList);
        DOS_ASSERT(0);
        sc_logr_error(SC_FUNC, "Num Transform Delete FAIL.ID %u does not exist.", ulCustomerID);
        return DOS_FAIL;
    }

    dll_delete(&g_stCustomerList, pstListNode);
    pthread_mutex_unlock(&g_mutexCustomerList);
    dos_dmem_free(pstListNode->pHandle);
    pstListNode->pHandle= NULL;
    dos_dmem_free(pstListNode);
    pstListNode = NULL;
    sc_logr_debug(SC_FUNC, "Delete Num Transform SUCC.(ID:%u)", ulCustomerID);

    return DOS_SUCC;
}

/* 增加一个号码与号码组的关系 */
U32 sc_caller_assign_add(U32 ulGrpID, U32 ulCallerID, U32 ulCallerType)
{
    SC_CALLER_QUERY_NODE_ST *pstCaller = NULL;
    SC_DID_NODE_ST          *pstDid = NULL;
    SC_CALLER_CACHE_NODE_ST *pstCache = NULL;
    HASH_NODE_S *pstHashNode = NULL;
    U32 ulHashIndex = U32_BUTT;

    pstCache = (SC_CALLER_CACHE_NODE_ST *)dos_dmem_alloc(sizeof(SC_CALLER_CACHE_NODE_ST));
    if (DOS_ADDR_INVALID(pstCache))
    {
        DOS_ASSERT(0);
        return DOS_FAIL;
    }
    pstCache->ulType= ulCallerType;

    if (SC_NUMBER_TYPE_CFG == ulCallerType)
    {
        pstCaller = (SC_CALLER_QUERY_NODE_ST *)dos_dmem_alloc(sizeof(SC_CALLER_QUERY_NODE_ST));
        if (!pstCaller)
        {
            DOS_ASSERT(0);
            dos_dmem_free(pstCache);
            pstCache = NULL;
            sc_logr_error(SC_FUNC, "Add Caller Assign FAIL.(ulGrpID:%u, ulCallerID:%u).", ulGrpID, ulCallerID);
            return DOS_FAIL;
        }
        pstCache->stData.pstCaller = pstCaller;
        pstCaller->ulIndexInDB = ulCallerID;
    }
    else if (SC_NUMBER_TYPE_DID == ulCallerType)
    {
        pstDid = (SC_DID_NODE_ST *)dos_dmem_alloc(sizeof(SC_DID_NODE_ST));
        if (!pstDid)
        {
            DOS_ASSERT(0);
            dos_dmem_free(pstCache);
            pstCache = NULL;
            sc_logr_error(SC_FUNC, "Add Caller Assign FAIL.(ulGrpID:%u, ulCallerID:%u).", ulGrpID, ulCallerID);
            return DOS_FAIL;
        }
        pstCache->stData.pstDid = pstDid;
        pstDid->ulDIDID = ulCallerID;
    }

    pstHashNode = (HASH_NODE_S *)dos_dmem_alloc(sizeof(HASH_NODE_S));
    if (!pstHashNode)
    {
        dos_dmem_free(pstCache);
        pstCache = NULL;
        if (DOS_ADDR_VALID(pstCaller))
        {
            dos_dmem_free(pstCaller);
            pstCaller = NULL;
        }
        if (DOS_ADDR_VALID(pstDid))
        {
            dos_dmem_free(pstDid);
            pstDid = NULL;
        }
        sc_logr_error(SC_FUNC, "Add Caller Assign FAIL.(ulGrpID:%u, ulCallerID:%u).", ulGrpID, ulCallerID);
        DOS_ASSERT(0);
        return DOS_FAIL;
    }
    HASH_Init_Node(pstHashNode);
    pstHashNode->pHandle = (VOID *)pstCache;

    ulHashIndex = sc_ep_caller_grp_hash_func(ulGrpID);
    hash_add_node(g_pstHashCallerGrp, pstHashNode, ulHashIndex, NULL);

    sc_logr_info(SC_FUNC, "Add Caller Assign SUCC.(ulGrpID:%u, ulCallerID:%u).", ulGrpID, ulCallerID);

    return DOS_SUCC;
}

/* 将号码从一个组中删除 */
U32 sc_caller_assign_delete(U32 ulGrpID, U32 ulCallerID, U32 ulCallerType)
{
    HASH_NODE_S  *pstHashNode = NULL;
    DLL_NODE_S   *pstNode  = NULL;
    U32  ulHashIndex = U32_BUTT;
    SC_CALLER_CACHE_NODE_ST *pstCache = NULL;
    SC_CALLER_GRP_NODE_ST *pstCallerGrp = NULL;
    BOOL  bFound = DOS_FALSE;

    HASH_Scan_Table(g_pstHashCallerGrp, ulHashIndex)
    {
        HASH_Scan_Bucket(g_pstHashCallerGrp, ulHashIndex, pstHashNode, HASH_NODE_S *)
        {
            if (DOS_ADDR_INVALID(pstHashNode)
                || DOS_ADDR_INVALID(pstHashNode->pHandle))
            {
                continue;
            }

            pstCallerGrp = (SC_CALLER_GRP_NODE_ST *)pstHashNode->pHandle;
            if (ulGrpID == pstCallerGrp->ulID)
            {
                bFound = DOS_TRUE;
                break;
            }
        }
        if (DOS_TRUE == bFound)
        {
            break;
        }
    }
    if (!bFound)
    {
        DOS_ASSERT(0);
        sc_logr_error(SC_FUNC, "Delete Caller Assign FAIL.(ulGrpID:%u, ulCallerID:%u)", ulGrpID, ulCallerID);
        return DOS_FAIL;
    }

    bFound = DOS_FALSE;
    DLL_Scan(&pstCallerGrp->stCallerList, pstNode, DLL_NODE_S *)
    {
        if (DOS_ADDR_INVALID(pstNode)
            || pstNode->pHandle)
        {
            continue;
        }
        pstCache = (SC_CALLER_CACHE_NODE_ST *)pstNode->pHandle;
        if (ulCallerType != pstCache->ulType)
        {
            continue;
        }

        if (SC_NUMBER_TYPE_CFG == ulCallerType)
        {
            if (ulCallerID == pstCache->stData.pstCaller->ulIndexInDB)
            {
                bFound = DOS_TRUE;
                break;
            }
        }
        else if (SC_NUMBER_TYPE_DID == ulCallerType)
        {
            if (ulCallerID == pstCache->stData.pstDid->ulDIDID)
            {
                bFound = DOS_TRUE;
                break;
            }
        }
    }
    if (!bFound)
    {
        DOS_ASSERT(0);
        return DOS_FAIL;
    }

    dll_delete(&pstCallerGrp->stCallerList, pstNode);
    dos_dmem_free(pstCache);
    pstCache = NULL;
    sc_logr_info(SC_FUNC, "Delete Caller Assign SUCC.(ulGrpID:%u, ulCallerID:%u)", ulGrpID, ulCallerID);

    return DOS_SUCC;
}

U32 sc_gateway_delete(U32 ulGatewayID)
{
    HASH_NODE_S   *pstHashNode = NULL;
    SC_GW_NODE_ST *pstGateway  = NULL;
    U32  ulIndex = U32_BUTT;

    ulIndex = sc_ep_gw_hash_func(ulGatewayID);
    pthread_mutex_lock(&g_mutexHashGW);
    pstHashNode = hash_find_node(g_pstHashGW, ulIndex, (VOID *)&ulGatewayID, sc_ep_gw_hash_find);
    if (DOS_ADDR_INVALID(pstHashNode)
        || DOS_ADDR_INVALID(pstHashNode->pHandle))
    {
        pthread_mutex_unlock(&g_mutexHashGW);
        DOS_ASSERT(0);
        return DOS_FAIL;
    }

    pstGateway = pstHashNode->pHandle;
    pstHashNode->pHandle = NULL;

    hash_delete_node(g_pstHashGW, pstHashNode, ulIndex);

    if (pstHashNode)
    {
        dos_dmem_free(pstHashNode);
        pstHashNode = NULL;
    }

    if (pstGateway)
    {
        dos_dmem_free(pstGateway);
        pstHashNode = NULL;
    }

    pthread_mutex_unlock(&g_mutexHashGW);

    return DOS_SUCC;
}

U32 sc_route_delete(U32 ulRouteID)
{
    DLL_NODE_S       *pstDLLNode   = NULL;
    SC_ROUTE_NODE_ST *pstRouteNode = NULL;

    pthread_mutex_lock(&g_mutexRouteList);
    pstDLLNode = dll_find(&g_stRouteList, (VOID *)&ulRouteID, sc_ep_route_find);
    if (DOS_ADDR_INVALID(pstDLLNode))
    {
        pthread_mutex_unlock(&g_mutexRouteList);
        DOS_ASSERT(0);
        return DOS_FAIL;
    }
    pstRouteNode = pstDLLNode->pHandle;
    pstDLLNode->pHandle =  NULL;

    dll_delete(&g_stRouteList, pstDLLNode);

    if (pstRouteNode)
    {
        dos_dmem_free(pstRouteNode);
        pstRouteNode = NULL;
    }

    if (pstDLLNode)
    {
        dos_dmem_free(pstDLLNode);
        pstDLLNode = NULL;
    }

    pthread_mutex_unlock(&g_mutexRouteList);

    return DOS_SUCC;
}

U32 sc_gateway_grp_delete(U32 ulGwGroupID)
{
    DLL_NODE_S         *pstDLLNode     = NULL;
    HASH_NODE_S        *pstHashNode    = NULL;
    SC_GW_GRP_NODE_ST  *pstGwGroupNode = NULL;
    U32 ulIndex = U32_BUTT;

    /* 获得网关组索引 */
    ulIndex = sc_ep_gw_grp_hash_func(ulGwGroupID);

    pthread_mutex_lock(&g_mutexHashGWGrp);

    /* 查找网关组节点首地址 */
    pstHashNode = hash_find_node(g_pstHashGWGrp, ulIndex, (VOID *)&ulGwGroupID, sc_ep_gw_grp_hash_find);
    if (DOS_ADDR_INVALID(pstHashNode)
        || DOS_ADDR_INVALID(pstHashNode->pHandle))
    {
        pthread_mutex_unlock(&g_mutexHashGW);
        DOS_ASSERT(0);
        return DOS_FAIL;
    }

    pstGwGroupNode = pstHashNode->pHandle;
    pstHashNode->pHandle = NULL;

    /* 删除节点 */
    hash_delete_node(g_pstHashGWGrp, pstHashNode, ulIndex);

    if (pstGwGroupNode)
    {
        pthread_mutex_lock(&pstGwGroupNode->mutexGWList);
        while (1)
        {
            if (DLL_Count(&pstGwGroupNode->stGWList) == 0)
            {
                break;
            }

            pstDLLNode = dll_fetch(&pstGwGroupNode->stGWList);
            if (DOS_ADDR_INVALID(pstDLLNode))
            {
                DOS_ASSERT(0);
                break;
            }

            /* dll节点的数据域勿要删除 */

            DLL_Init_Node(pstDLLNode);
            dos_dmem_free(pstDLLNode);
            pstDLLNode = NULL;
        }
        pthread_mutex_unlock(&pstGwGroupNode->mutexGWList);


        pthread_mutex_destroy(&pstGwGroupNode->mutexGWList);
        dos_dmem_free(pstGwGroupNode);
        pstGwGroupNode = NULL;
    }

    if (pstHashNode)
    {
       dos_dmem_free(pstHashNode);
       pstHashNode = NULL;
    }

    pthread_mutex_unlock(&g_mutexHashGWGrp);

    return DOS_SUCC;
}

U32 sc_did_delete(U32 ulDidID)
{
    HASH_NODE_S     *pstHashNode = NULL;
    SC_DID_NODE_ST  *pstDidNode  = NULL;
    BOOL blFound = DOS_FALSE;
    U32 ulIndex = U32_BUTT;

    pthread_mutex_lock(&g_mutexHashDIDNum);
    HASH_Scan_Table(g_pstHashDIDNum, ulIndex)
    {
        HASH_Scan_Bucket(g_pstHashDIDNum, ulIndex, pstHashNode, HASH_NODE_S*)
        {
            if (DOS_ADDR_INVALID(pstHashNode))
            {
                break;
            }

            pstDidNode = pstHashNode->pHandle;
            if (DOS_ADDR_INVALID(pstDidNode))
            {
                continue;
            }

            if (ulDidID == pstDidNode->ulDIDID)
            {
                blFound = DOS_TRUE;
                break;
            }
        }

        if (blFound)
        {
            break;
        }
    }

    if (blFound)
    {
        ulIndex = sc_sip_did_hash_func(pstDidNode->szDIDNum);

        /* 删除节点 */
        hash_delete_node(g_pstHashDIDNum, pstHashNode, ulIndex);

        if (pstHashNode)
        {
            dos_dmem_free(pstHashNode);
            pstHashNode = NULL;
        }

        if (pstDidNode)
        {
           dos_dmem_free(pstDidNode);
           pstDidNode = NULL;
        }
    }

    pthread_mutex_unlock(&g_mutexHashDIDNum);

    if (blFound)
    {
        return DOS_SUCC;
    }
    else
    {
        return DOS_FALSE;
    }
}

U32 sc_black_list_delete(U32 ulFileID)
{
    HASH_NODE_S        *pstHashNode  = NULL;
    SC_BLACK_LIST_NODE *pstBlackList = NULL;
    U32  ulHashIndex = 0;

    pthread_mutex_lock(&g_mutexHashBlackList);

    HASH_Scan_Table(g_pstHashBlackList, ulHashIndex)
    {
        HASH_Scan_Bucket(g_pstHashBlackList, ulHashIndex, pstHashNode, HASH_NODE_S *)
        {
            if (DOS_ADDR_INVALID(pstHashNode)
                || DOS_ADDR_INVALID(pstHashNode->pHandle))
            {
                continue;
            }

            pstBlackList = (SC_BLACK_LIST_NODE *)pstHashNode->pHandle;
            /* 如果找到和该fileID相同，则从哈希表中删除*/
            if (pstBlackList->ulFileID == ulFileID)
            {
                hash_delete_node(g_pstHashBlackList, pstHashNode, ulHashIndex);
                dos_dmem_free(pstHashNode);
                pstHashNode = NULL;

                dos_dmem_free(pstBlackList);
                pstBlackList = NULL;
                break;
            }
        }
    }
    pthread_mutex_unlock(&g_mutexHashBlackList);

    return DOS_SUCC;
}


/**
 * 函数: S32 sc_load_sip_userid_cb(VOID *pArg, S32 lCount, S8 **aszValues, S8 **aszNames)
 * 功能: 加载SIP账户时数据库查询的回调函数，将数据加入SIP账户的HASH表中
 * 参数:
 *      VOID *pArg: 参数
 *      S32 lCount: 列数量
 *      S8 **aszValues: 值裂变
 *      S8 **aszNames: 字段名列表
 * 返回值: 成功返回DOS_SUCC，否则返回DOS_FAIL
 */
S32 sc_load_sip_userid_cb(VOID *pArg, S32 lCount, S8 **aszValues, S8 **aszNames)
{
    SC_USER_ID_NODE_ST *pstSIPUserIDNodeNew = NULL;
    SC_USER_ID_NODE_ST *pstSIPUserIDNode = NULL;
    HASH_NODE_S        *pstHashNode      = NULL;
    BOOL               blProcessOK       = DOS_FALSE;
    U32                ulHashIndex       = 0;
    S32                lIndex            = 0;

    if (DOS_ADDR_INVALID(aszNames)
        || DOS_ADDR_INVALID(aszValues))
    {
        DOS_ASSERT(0);
        return DOS_FAIL;
    }

    pstSIPUserIDNodeNew = (SC_USER_ID_NODE_ST *)dos_dmem_alloc(sizeof(SC_USER_ID_NODE_ST));
    if (DOS_ADDR_INVALID(pstSIPUserIDNodeNew))
    {
        DOS_ASSERT(0);

        return DOS_FAIL;
    }
    sc_ep_sip_userid_init(pstSIPUserIDNodeNew);

    for (lIndex=0, blProcessOK = DOS_TRUE; lIndex < lCount; lIndex++)
    {
        if (0 == dos_strnicmp(aszNames[lIndex], "id", dos_strlen("id")))
        {
            if (dos_atoul(aszValues[lIndex], &pstSIPUserIDNodeNew->ulSIPID) < 0)
            {
                blProcessOK = DOS_FALSE;
                break;
            }
        }
        else if (0 == dos_strnicmp(aszNames[lIndex], "customer_id", dos_strlen("customer_id")))
        {
            if (dos_atoul(aszValues[lIndex], &pstSIPUserIDNodeNew->ulCustomID) < 0)
            {
                blProcessOK = DOS_FALSE;
                break;
            }
        }
        else if (0 == dos_strnicmp(aszNames[lIndex], "userid", dos_strlen("userid")))
        {
            if (DOS_ADDR_INVALID(aszValues[lIndex])
                || '\0' == aszValues[lIndex][0])
            {
                blProcessOK = DOS_FALSE;
                break;
            }

            dos_strncpy(pstSIPUserIDNodeNew->szUserID, aszValues[lIndex], sizeof(pstSIPUserIDNodeNew->szUserID));
            pstSIPUserIDNodeNew->szUserID[sizeof(pstSIPUserIDNodeNew->szUserID) - 1] = '\0';
        }
        else if (0 == dos_strnicmp(aszNames[lIndex], "extension", dos_strlen("extension")))
        {
            if (DOS_ADDR_VALID(aszValues[lIndex])
                && '\0' != aszValues[lIndex][0])
            {
                dos_strncpy(pstSIPUserIDNodeNew->szExtension, aszValues[lIndex], sizeof(pstSIPUserIDNodeNew->szExtension));
                pstSIPUserIDNodeNew->szExtension[sizeof(pstSIPUserIDNodeNew->szExtension) - 1] = '\0';
            }
        }
    }

    if (!blProcessOK)
    {
        DOS_ASSERT(0);

        dos_dmem_free(pstSIPUserIDNodeNew);
        pstSIPUserIDNodeNew = NULL;
        return DOS_FAIL;
    }

    pthread_mutex_lock(&g_mutexHashSIPUserID);

    ulHashIndex = sc_sip_userid_hash_func(pstSIPUserIDNodeNew->szUserID);
    pstHashNode = hash_find_node(g_pstHashSIPUserID, ulHashIndex, (VOID *)&pstSIPUserIDNodeNew->szUserID, sc_ep_sip_userid_hash_find);

    if (DOS_ADDR_INVALID(pstHashNode))
    {
        pstHashNode = (HASH_NODE_S *)dos_dmem_alloc(sizeof(HASH_NODE_S));
        if (DOS_ADDR_INVALID(pstHashNode))
        {
            DOS_ASSERT(0);
            dos_dmem_free(pstSIPUserIDNodeNew);
            pstSIPUserIDNodeNew = NULL;
            pthread_mutex_unlock(&g_mutexHashSIPUserID);
            return DOS_FAIL;
        }
/*
        sc_logr_info(SC_ESL, "Load SIP User. ID: %d, Customer: %d, UserID: %s, Extension: %s"
                    , pstSIPUserIDNodeNew->ulSIPID
                    , pstSIPUserIDNodeNew->ulCustomID
                    , pstSIPUserIDNodeNew->szUserID
                    , pstSIPUserIDNodeNew->szExtension);
*/

        HASH_Init_Node(pstHashNode);
        pstHashNode->pHandle = pstSIPUserIDNodeNew;

        hash_add_node(g_pstHashSIPUserID, (HASH_NODE_S *)pstHashNode, ulHashIndex, NULL);
    }
    else
    {
        pstSIPUserIDNode = (SC_USER_ID_NODE_ST *)pstHashNode->pHandle;
        pstSIPUserIDNode->ulCustomID = pstSIPUserIDNodeNew->ulCustomID;

        dos_strncpy(pstSIPUserIDNode->szUserID, pstSIPUserIDNodeNew->szUserID, sizeof(pstSIPUserIDNode->szUserID));
        pstSIPUserIDNode->szUserID[sizeof(pstSIPUserIDNode->szUserID) - 1] = '\0';

        dos_strncpy(pstSIPUserIDNode->szExtension, pstSIPUserIDNodeNew->szExtension, sizeof(pstSIPUserIDNode->szExtension));
        pstSIPUserIDNode->szExtension[sizeof(pstSIPUserIDNode->szExtension) - 1] = '\0';

        dos_dmem_free(pstSIPUserIDNodeNew);
        pstSIPUserIDNodeNew = NULL;
    }
    //

    pthread_mutex_unlock(&g_mutexHashSIPUserID);

    return DOS_SUCC;
}

/**
 * 函数: U32 sc_load_sip_userid()
 * 功能: 加载SIP账户数据
 * 参数:
 * 返回值: 成功返回DOS_SUCC，否则返回DOS_FAIL
 */
U32 sc_load_sip_userid(U32 ulIndex)
{
    S8 szSQL[1024] = {0, };

    if (SC_INVALID_INDEX == ulIndex)
    {
        dos_snprintf(szSQL, sizeof(szSQL), "SELECT id, customer_id, extension,userid FROM tbl_sip where tbl_sip.status = 0;");
    }
    else
    {
        dos_snprintf(szSQL, sizeof(szSQL), "SELECT id, customer_id, extension,userid FROM tbl_sip where tbl_sip.status = 0 AND id=%d;", ulIndex);
    }

    if (db_query(g_pstSCDBHandle, szSQL, sc_load_sip_userid_cb, NULL, NULL) != DB_ERR_SUCC)
    {
        DOS_ASSERT(0);

        sc_logr_error(SC_ESL, "%s", "Load sip account fail.");
        return DOS_FAIL;
    }

    return DOS_SUCC;
}

/**
 * 函数: S32 sc_load_black_list_cb()
 * 功能: 加载Black时数据库查询的回调函数，将数据加入黑名单hash表
 * 参数:
 *      VOID *pArg: 参数
 *      S32 lCount: 列数量
 *      S8 **aszValues: 值裂变
 *      S8 **aszNames: 字段名列表
 * 返回值: 成功返回DOS_SUCC，否则返回DOS_FAIL
 */
S32 sc_load_black_list_cb(VOID *pArg, S32 lCount, S8 **aszValues, S8 **aszNames)
{
    SC_BLACK_LIST_NODE *pstBlackListNode = NULL;
    SC_BLACK_LIST_NODE *pstBlackListTmp  = NULL;
    HASH_NODE_S        *pstHashNode      = NULL;
    BOOL               blProcessOK       = DOS_TRUE;
    S32                lIndex            = 0;
    U32                ulHashIndex       = 0;


    if (DOS_ADDR_INVALID(aszValues)
        || DOS_ADDR_INVALID(aszNames))
    {
        DOS_ASSERT(0);
        return DOS_FAIL;
    }

    pstBlackListNode = dos_dmem_alloc(sizeof(SC_BLACK_LIST_NODE));
    if (DOS_ADDR_INVALID(pstBlackListNode))
    {
        DOS_ASSERT(0);
        return DOS_FAIL;
    }

    sc_ep_black_init(pstBlackListNode);

    for (blProcessOK = DOS_FALSE, lIndex = 0; lIndex < lCount; lIndex++)
    {
        if (0 == dos_strnicmp(aszNames[lIndex], "id", dos_strlen("id")))
        {
            if (DOS_ADDR_INVALID(aszValues[lIndex])
                || dos_atoul(aszValues[lIndex], &pstBlackListNode->ulID) < 0)
            {
                blProcessOK = DOS_FALSE;

                break;
            }
        }
        else if (0 == dos_strnicmp(aszNames[lIndex], "customer_id", dos_strlen("customer_id")))
        {
            if (DOS_ADDR_INVALID(aszValues[lIndex])
                || dos_atoul(aszValues[lIndex], &pstBlackListNode->ulCustomerID) < 0)
            {
                blProcessOK = DOS_FALSE;
                break;
            }
        }
        else if (0 == dos_strnicmp(aszNames[lIndex], "file_id", dos_strlen("file_id")))
        {
            if (DOS_ADDR_INVALID(aszValues[lIndex])
                || dos_atoul(aszValues[lIndex], &pstBlackListNode->ulFileID) < 0)
            {
                blProcessOK = DOS_FALSE;
                break;
            }
        }
        else if (0 == dos_strnicmp(aszNames[lIndex], "number", dos_strlen("number")))
        {
            if (DOS_ADDR_INVALID(aszValues[lIndex])
                || '\0' == aszValues[lIndex][0])
            {
                blProcessOK = DOS_FALSE;
                break;
            }

            dos_strncpy(pstBlackListNode->szNum, aszValues[lIndex], sizeof(pstBlackListNode->szNum));
            pstBlackListNode->szNum[sizeof(pstBlackListNode->szNum) - 1] = '\0';
        }
        else if (0 == dos_strnicmp(aszNames[lIndex], "type", dos_strlen("type")))
        {
            if (DOS_ADDR_INVALID(aszValues[lIndex])
                || dos_atoul(aszValues[lIndex], &pstBlackListNode->enType) < 0)
            {
                blProcessOK = DOS_FALSE;
                break;
            }
        }

        blProcessOK = DOS_TRUE;
    }

    if (!blProcessOK)
    {
        DOS_ASSERT(0);

        dos_dmem_free(pstBlackListNode);
        return DOS_FAIL;
    }

    ulHashIndex = sc_ep_black_list_hash_func(pstBlackListNode->ulID);
    pstHashNode = hash_find_node(g_pstHashBlackList, ulHashIndex, (VOID *)&pstBlackListNode->ulID, sc_ep_black_list_find);
    if (DOS_ADDR_INVALID(pstHashNode))
    {
        pstHashNode = dos_dmem_alloc(sizeof(HASH_NODE_S));
        if (DOS_ADDR_INVALID(pstHashNode ))
        {
            DOS_ASSERT(0);

            dos_dmem_free(pstBlackListNode);
            pstBlackListNode = NULL;
            return DOS_FAIL;
        }

        HASH_Init_Node(pstHashNode);
        pstHashNode->pHandle = pstBlackListNode;
        ulHashIndex = sc_ep_black_list_hash_func(pstBlackListNode->ulID);

        pthread_mutex_lock(&g_mutexHashBlackList);
        hash_add_node(g_pstHashBlackList, pstHashNode, ulHashIndex, NULL);
        pthread_mutex_unlock(&g_mutexHashBlackList);
    }
    else
    {
        pstBlackListTmp = pstHashNode->pHandle;
        if (DOS_ADDR_INVALID(pstBlackListTmp))
        {
            DOS_ASSERT(0);
            dos_dmem_free(pstBlackListNode);
            pstBlackListNode = NULL;
            return DOS_FAIL;
        }

        pstBlackListTmp->enType = pstBlackListNode->enType;

        dos_strncpy(pstBlackListTmp->szNum, pstBlackListNode->szNum, sizeof(pstBlackListTmp->szNum));
        pstBlackListTmp->szNum[sizeof(pstBlackListTmp->szNum) - 1] = '\0';
        dos_dmem_free(pstBlackListNode);
        pstBlackListNode = NULL;
    }

    return DOS_SUCC;
}


/**
 * 函数: U32 sc_load_black_list()
 * 功能: 加载黑名单数据
 * 参数:
 * 返回值: 成功返回DOS_SUCC，否则返回DOS_FAIL
 */
U32 sc_load_black_list(U32 ulIndex)
{
    S8 szSQL[1024] = { 0, };

    if (SC_INVALID_INDEX == ulIndex)
    {
        dos_snprintf(szSQL, sizeof(szSQL), "SELECT id, customer_id, file_id, number, type FROM tbl_blacklist_pool;");
    }
    else
    {
        dos_snprintf(szSQL, sizeof(szSQL), "SELECT id, customer_id, file_id, number, type FROM tbl_blacklist_pool WHERE file_id=%u;", ulIndex);
    }

    if (db_query(g_pstSCDBHandle, szSQL, sc_load_black_list_cb, NULL, NULL) != DB_ERR_SUCC)
    {
        DOS_ASSERT(0);

        sc_logr_error(SC_ESL, "%s", "Load sip account fail.");
        return DOS_FAIL;
    }

    return DOS_SUCC;
}

static S32 sc_load_tt_number_cb(VOID *pArg, S32 lCount, S8 **aszValues, S8 **aszNames)
{
    HASH_NODE_S        *pstHashNode      = NULL;
    SC_TT_NODE_ST      *pstSCTTNumber    = NULL;
    SC_TT_NODE_ST      *pstSCTTNumberOld = NULL;
    BOOL               blProcessOK       = DOS_FALSE;
    U32                ulHashIndex       = 0;
    S32                lIndex            = 0;

    if (DOS_ADDR_INVALID(aszNames)
        || DOS_ADDR_INVALID(aszValues))
    {
        DOS_ASSERT(0);

        return DOS_FAIL;
    }

    pstSCTTNumber = (SC_TT_NODE_ST *)dos_dmem_alloc(sizeof(SC_TT_NODE_ST));
    if (DOS_ADDR_INVALID(pstSCTTNumber))
    {
        DOS_ASSERT(0);

        return DOS_FAIL;
    }

    sc_ep_tt_init(pstSCTTNumber);

    for (lIndex=0, blProcessOK=DOS_TRUE; lIndex<lCount; lIndex++)
    {
        if (0 == dos_strnicmp(aszNames[lIndex], "id", dos_strlen("id")))
        {
            if (dos_atoul(aszValues[lIndex], &pstSCTTNumber->ulID) < 0)
            {
                blProcessOK = DOS_FALSE;
                break;
            }
        }
        else if (0 == dos_strnicmp(aszNames[lIndex], "ip_port", dos_strlen("ip_port")))
        {
            if (DOS_ADDR_INVALID(aszValues[lIndex])
                || '\0' == aszValues[lIndex][0])
            {
                blProcessOK = DOS_FALSE;
                break;
            }

            dos_strncpy(pstSCTTNumber->szAddr, aszValues[lIndex], sizeof(pstSCTTNumber->szAddr));
            pstSCTTNumber->szAddr[sizeof(pstSCTTNumber->szAddr) - 1] = '\0';
        }
        else if (0 == dos_strnicmp(aszNames[lIndex], "prefix_number", dos_strlen("prefix_number")))
        {
            if (DOS_ADDR_INVALID(aszValues[lIndex])
                || '\0' == aszValues[lIndex][0])
            {
                blProcessOK = DOS_FALSE;
                break;
            }

            dos_strncpy(pstSCTTNumber->szPrefix, aszValues[lIndex], sizeof(pstSCTTNumber->szPrefix));
            pstSCTTNumber->szPrefix[sizeof(pstSCTTNumber->szPrefix) - 1] = '\0';
        }
    }

    if (!blProcessOK)
    {
        DOS_ASSERT(0);

        dos_dmem_free(pstSCTTNumber);
        pstSCTTNumber = NULL;
        return DOS_FALSE;
    }


    pthread_mutex_lock(&g_mutexHashTTNumber);
    ulHashIndex = sc_ep_tt_hash_func(pstSCTTNumber->ulID);
    pstHashNode = hash_find_node(g_pstHashTTNumber, ulHashIndex, &pstSCTTNumber->ulID, sc_ep_tt_list_find);
    if (DOS_ADDR_VALID(pstHashNode))
    {
        if (DOS_ADDR_VALID(pstHashNode->pHandle))
        {
            pstSCTTNumberOld = pstHashNode->pHandle;
            dos_snprintf(pstSCTTNumberOld->szAddr, sizeof(pstSCTTNumberOld->szAddr), pstSCTTNumber->szAddr);
            dos_snprintf(pstSCTTNumberOld->szPrefix, sizeof(pstSCTTNumberOld->szPrefix), pstSCTTNumber->szPrefix);

            dos_dmem_free(pstSCTTNumber);
            pstSCTTNumber = NULL;
        }
        else
        {
            pstHashNode->pHandle = pstSCTTNumber;
        }
    }
    else
    {
        pstHashNode = dos_dmem_alloc(sizeof(HASH_NODE_S));
        if (DOS_ADDR_INVALID(pstHashNode))
        {
            DOS_ASSERT(0);

            pthread_mutex_unlock(&g_mutexHashTTNumber);
            dos_dmem_free(pstSCTTNumber);
            pstSCTTNumber = NULL;
            return DOS_FALSE;
        }

        pstHashNode->pHandle = pstSCTTNumber;

        hash_add_node(g_pstHashTTNumber, pstHashNode, ulHashIndex, NULL);
    }
    pthread_mutex_unlock(&g_mutexHashTTNumber);

    return DOS_SUCC;
}


U32 sc_load_tt_number(U32 ulIndex)
{
    S8 szSQL[1024] = { 0, };

    if (SC_INVALID_INDEX == ulIndex)
    {
        dos_snprintf(szSQL, sizeof(szSQL), "SELECT id, ip_port, prefix_number FROM tbl_eix");
    }
    else
    {
        dos_snprintf(szSQL, sizeof(szSQL), "SELECT id, ip_port, prefix_number FROM tbl_eix WHERE id=%u", ulIndex);
    }

    if (db_query(g_pstSCDBHandle, szSQL, sc_load_tt_number_cb, NULL, NULL) != DB_ERR_SUCC)
    {
        DOS_ASSERT(0);

        sc_logr_error(SC_ESL, "%s", "Load TT number fail.");
        return DOS_FAIL;
    }

    return DOS_SUCC;

}

/**
 * 函数: S32 sc_load_did_number_cb(VOID *pArg, S32 lCount, S8 **aszValues, S8 **aszNames)
 * 功能: 加载DID号码时数据库查询的回调函数，将数据加入DID号码的HASH表中
 * 参数:
 *      VOID *pArg: 参数
 *      S32 lCount: 列数量
 *      S8 **aszValues: 值裂变
 *      S8 **aszNames: 字段名列表
 * 返回值: 成功返回DOS_SUCC，否则返回DOS_FAIL
 */
S32 sc_load_did_number_cb(VOID *pArg, S32 lCount, S8 **aszValues, S8 **aszNames)
{
    SC_DID_NODE_ST     *pstDIDNumNode    = NULL;
    SC_DID_NODE_ST     *pstDIDNumTmp     = NULL;
    HASH_NODE_S        *pstHashNode      = NULL;
    BOOL               blProcessOK       = DOS_FALSE;
    U32                ulHashIndex       = 0;
    S32                lIndex            = 0;

    if (DOS_ADDR_INVALID(aszNames)
        || DOS_ADDR_INVALID(aszValues))
    {
        DOS_ASSERT(0);

        return DOS_FAIL;
    }

    pstDIDNumNode = (SC_DID_NODE_ST *)dos_dmem_alloc(sizeof(SC_DID_NODE_ST));
    if (DOS_ADDR_INVALID(pstDIDNumNode))
    {
        DOS_ASSERT(0);

        return DOS_FAIL;
    }

    sc_ep_did_init(pstDIDNumNode);

    for (lIndex=0, blProcessOK=DOS_TRUE; lIndex<lCount; lIndex++)
    {
        if (0 == dos_strnicmp(aszNames[lIndex], "id", dos_strlen("id")))
        {
            if (dos_atoul(aszValues[lIndex], &pstDIDNumNode->ulDIDID) < 0)
            {
                blProcessOK = DOS_FALSE;
                break;
            }
        }
        else if (0 == dos_strnicmp(aszNames[lIndex], "customer_id", dos_strlen("customer_id")))
        {
            if (dos_atoul(aszValues[lIndex], &pstDIDNumNode->ulCustomID) < 0)
            {
                blProcessOK = DOS_FALSE;
            }
        }
        else if (0 == dos_strnicmp(aszNames[lIndex], "did_number", dos_strlen("did_number")))
        {
            if (DOS_ADDR_INVALID(aszValues[lIndex])
                || '\0' == aszValues[lIndex][0])
            {
                blProcessOK = DOS_FALSE;
                break;
            }

            dos_strncpy(pstDIDNumNode->szDIDNum, aszValues[lIndex], sizeof(pstDIDNumNode->szDIDNum));
            pstDIDNumNode->szDIDNum[sizeof(pstDIDNumNode->szDIDNum) - 1] = '\0';
        }
        else if (0 == dos_strnicmp(aszNames[lIndex], "bind_type", dos_strlen("bind_type")))
        {
            if (dos_atoul(aszValues[lIndex], &pstDIDNumNode->ulBindType) < 0
                || pstDIDNumNode->ulBindType >= SC_DID_BIND_TYPE_BUTT)
            {
                blProcessOK = DOS_FALSE;
                break;
            }
        }
        else if (0 == dos_strnicmp(aszNames[lIndex], "bind_id", dos_strlen("bind_id")))
        {
            if (dos_atoul(aszValues[lIndex], &pstDIDNumNode->ulBindID) < 0)
            {
                blProcessOK = DOS_FALSE;
                break;
            }
        }
        else if (0 == dos_strnicmp(aszNames[lIndex], "status", dos_strlen("status")))
        {
            if (dos_atoul(aszValues[lIndex], &pstDIDNumNode->bValid) < 0)
            {
                blProcessOK = DOS_FALSE;
                break;
            }
        }
    }

    if (!blProcessOK)
    {
        DOS_ASSERT(0);

        dos_dmem_free(pstDIDNumNode);
        pstDIDNumNode = NULL;
        return DOS_FALSE;
    }

    pthread_mutex_lock(&g_mutexHashDIDNum);
    ulHashIndex = sc_sip_did_hash_func(pstDIDNumNode->szDIDNum);
    pstHashNode = hash_find_node(g_pstHashDIDNum, ulHashIndex, (VOID *)pstDIDNumNode->szDIDNum, sc_ep_did_hash_find);
    if (DOS_ADDR_INVALID(pstHashNode))
    {
        pstHashNode = dos_dmem_alloc(sizeof(HASH_NODE_S));
        if (DOS_ADDR_INVALID(pstHashNode))
        {
            DOS_ASSERT(0);

            dos_dmem_free(pstDIDNumNode);
            pstDIDNumNode = NULL;
            pthread_mutex_unlock(&g_mutexHashDIDNum);
            return DOS_FALSE;
        }

        HASH_Init_Node(pstHashNode);
        pstHashNode->pHandle = pstDIDNumNode;
        ulHashIndex = sc_sip_did_hash_func(pstDIDNumNode->szDIDNum);

        hash_add_node(g_pstHashDIDNum, (HASH_NODE_S *)pstHashNode, ulHashIndex, NULL);

    }
    else
    {
        pstDIDNumTmp = pstHashNode->pHandle;
        if (DOS_ADDR_INVALID(pstDIDNumTmp))
        {
            DOS_ASSERT(0);

            dos_dmem_free(pstDIDNumNode);
            pstDIDNumNode = NULL;

            pthread_mutex_unlock(&g_mutexHashDIDNum);
            return DOS_FAIL;
        }

        pstDIDNumTmp->ulCustomID = pstDIDNumNode->ulCustomID;
        pstDIDNumTmp->ulDIDID = pstDIDNumNode->ulDIDID;
        pstDIDNumTmp->ulBindType = pstDIDNumNode->ulBindType;
        pstDIDNumTmp->ulBindID  = pstDIDNumNode->ulBindID;
        pstDIDNumTmp->bValid = pstDIDNumNode->bValid;
        dos_strncpy(pstDIDNumTmp->szDIDNum, pstDIDNumNode->szDIDNum, sizeof(pstDIDNumTmp->szDIDNum));
        pstDIDNumTmp->szDIDNum[sizeof(pstDIDNumTmp->szDIDNum) - 1] = '\0';

        dos_dmem_free(pstDIDNumNode);
        pstDIDNumNode = NULL;
    }
    pthread_mutex_unlock(&g_mutexHashDIDNum);

    return DOS_SUCC;
}

/**
 * 函数: U32 sc_load_did_number()
 * 功能: 加载DID号码数据
 * 参数:
 * 返回值: 成功返回DOS_SUCC，否则返回DOS_FAIL
 */
U32 sc_load_did_number(U32 ulIndex)
{
    S8 szSQL[1024] = { 0, };

    if (SC_INVALID_INDEX == ulIndex)
    {
        dos_snprintf(szSQL, sizeof(szSQL), "SELECT id, customer_id, did_number, bind_type, bind_id, status FROM tbl_sipassign;");
    }
    else
    {
        dos_snprintf(szSQL, sizeof(szSQL), "SELECT id, customer_id, did_number, bind_type, bind_id, status FROM tbl_sipassign where id=%u;", ulIndex);
    }

    db_query(g_pstSCDBHandle, szSQL, sc_load_did_number_cb, NULL, NULL);

    return DOS_SUCC;
}

/**
 * 函数: S32 sc_load_gateway_cb(VOID *pArg, S32 lCount, S8 **aszValues, S8 **aszNames)
 * 功能: 加载网关列表数据时数据库查询的回调函数，将数据加入网关加入对于路由的列表中
 * 参数:
 *      VOID *pArg: 参数
 *      S32 lCount: 列数量
 *      S8 **aszValues: 值裂变
 *      S8 **aszNames: 字段名列表
 * 返回值: 成功返回DOS_SUCC，否则返回DOS_FAIL
 */
S32 sc_load_gateway_cb(VOID *pArg, S32 lCount, S8 **aszValues, S8 **aszNames)
{
    SC_GW_NODE_ST        *pstGWNode     = NULL;
    HASH_NODE_S          *pstHashNode   = NULL;
    S8 *pszGWID = NULL, *pszDomain = NULL, *pszStatus = NULL;
    U32 ulID, ulStatus;
    U32 ulHashIndex = U32_BUTT, ulRet = U32_BUTT;

    if (DOS_ADDR_INVALID(aszNames)
        || DOS_ADDR_INVALID(aszValues))
    {
        DOS_ASSERT(0);

        return DOS_FAIL;
    }

    pszGWID = aszValues[0];
    pszDomain = aszValues[1];
    pszStatus = aszValues[2];
    if (DOS_ADDR_INVALID(pszGWID)
        || DOS_ADDR_INVALID(pszDomain)
        || dos_atoul(pszGWID, &ulID) < 0
        || dos_atoul(pszStatus, &ulStatus) < 0)
    {
        DOS_ASSERT(0);

        return DOS_FAIL;
    }

    pthread_mutex_lock(&g_mutexHashGW);
    ulHashIndex = sc_ep_gw_hash_func(ulID);
    pstHashNode = hash_find_node(g_pstHashGW, ulHashIndex, (VOID *)&ulID, sc_ep_gw_hash_find);
    /* 此过程为了将数据库里的数据全部同步到内存，bExist为true标明这些数据来自于数据库 */
    if (DOS_ADDR_INVALID(pstHashNode))
    {
        /* 如果不存在则重新申请节点并加入内存 */
        pstHashNode = dos_dmem_alloc(sizeof(HASH_NODE_S));
        if (DOS_ADDR_INVALID(pstHashNode))
        {
            DOS_ASSERT(0);

            pthread_mutex_unlock(&g_mutexHashGW);
            return DOS_FAIL;
        }

        pstGWNode = dos_dmem_alloc(sizeof(SC_GW_NODE_ST));
        if (DOS_ADDR_INVALID(pstGWNode))
        {
            DOS_ASSERT(0);

            dos_dmem_free(pstHashNode);

            pthread_mutex_unlock(&g_mutexHashGW);
            return DOS_FAIL;
        }

        sc_ep_gw_init(pstGWNode);

        pstGWNode->ulGWID = ulID;
        if ('\0' != pszDomain[0])
        {
            dos_strncpy(pstGWNode->szGWDomain, pszDomain, sizeof(pstGWNode->szGWDomain));
            pstGWNode->szGWDomain[sizeof(pstGWNode->szGWDomain) - 1] = '\0';
        }
        else
        {
            pstGWNode->szGWDomain[0] = '\0';
        }
        pstGWNode->bStatus = ulStatus;

        HASH_Init_Node(pstHashNode);
        pstHashNode->pHandle = pstGWNode;
        pstGWNode->bExist = DOS_TRUE;

        ulHashIndex = sc_ep_gw_hash_func(pstGWNode->ulGWID);
        hash_add_node(g_pstHashGW, pstHashNode, ulHashIndex, NULL);
    }
    else
    {
        pstGWNode = pstHashNode->pHandle;
        if (DOS_ADDR_INVALID(pstGWNode))
        {
            DOS_ASSERT(0);

            pthread_mutex_unlock(&g_mutexHashGW);
            return DOS_FAIL;
        }

        dos_strncpy(pstGWNode->szGWDomain, pszDomain, sizeof(pstGWNode->szGWDomain));
        pstGWNode->szGWDomain[sizeof(pstGWNode->szGWDomain) - 1] = '\0';
        pstGWNode->bExist = DOS_TRUE;
        pstGWNode->bStatus = ulStatus;
    }
    pthread_mutex_unlock(&g_mutexHashGW);

    /* 在这里强制生成网关和删除网关 */
    if (DOS_FALSE == pstGWNode->bStatus)
    {
        ulRet = py_exec_func("router", "del_route", "(k)", (U64)pstGWNode->ulGWID);
        if (DOS_SUCC != ulRet)
        {
            sc_logr_debug(SC_FUNC, "Delete FreeSWITCH XML file of gateway %u FAIL by status.", pstGWNode->ulGWID);
        }
        else
        {
            sc_logr_debug(SC_FUNC, "Delete FreeSWITCH XML file of gateway %u SUCC by status.", pstGWNode->ulGWID);
        }
    }
    else
    {
        ulRet = py_exec_func("router", "make_route", "(i)", pstGWNode->ulGWID);
        if (DOS_SUCC != ulRet)
        {
            sc_logr_error(SC_FUNC, "Generate FreeSWITCH XML file of gateway %u FAIL by status.", pstGWNode->ulGWID);
        }
        else
        {
            sc_logr_debug(SC_FUNC, "Generate FreeSWITCH XML file of gateway %u SUCC by status.", pstGWNode->ulGWID);
        }
    }
    return DOS_SUCC;
}

/**
 * 函数: U32 sc_load_gateway(U32 ulIndex)
 * 功能: 加载网关数据
 * 参数:
 * 返回值: 成功返回DOS_SUCC，否则返回DOS_FAIL
 */
U32 sc_load_gateway(U32 ulIndex)
{
    S8 szSQL[1024] = {0,};
    U32 ulRet;

    if (SC_INVALID_INDEX == ulIndex)
    {
        dos_snprintf(szSQL, sizeof(szSQL)
                        , "SELECT id, realm, status FROM tbl_gateway;");
    }
    else
    {
        dos_snprintf(szSQL, sizeof(szSQL)
                        , "SELECT id, realm, status FROM tbl_gateway WHERE id=%d;", ulIndex);
    }

    ulRet = db_query(g_pstSCDBHandle, szSQL, sc_load_gateway_cb, NULL, NULL);
    if (DB_ERR_SUCC != ulRet)
    {
        DOS_ASSERT(0);
        sc_logr_error(SC_FUNC, "Load gateway FAIL.(ID:%u)", ulIndex);
        return DOS_FAIL;
    }
    sc_logr_info(SC_FUNC, "Load gateway SUCC.(ID:%u)", ulIndex);

    return DOS_SUCC;
}

/**
 * 函数: S32 sc_load_gateway_grp_cb(VOID *pArg, S32 lCount, S8 **aszValues, S8 **aszNames)
 * 功能: 加载网关组列表数据时数据库查询的回调函数
 * 参数:
 *      VOID *pArg: 参数
 *      S32 lCount: 列数量
 *      S8 **aszValues: 值裂变
 *      S8 **aszNames: 字段名列表
 * 返回值: 成功返回DOS_SUCC，否则返回DOS_FAIL
 **/
S32 sc_load_gateway_grp_cb(VOID *pArg, S32 lCount, S8 **aszValues, S8 **aszNames)
{
    SC_GW_GRP_NODE_ST    *pstGWGrpNode  = NULL;
    HASH_NODE_S          *pstHashNode   = NULL;
    S8 *pszGWGrpID = NULL;
    U32 ulID = 0;
    U32 ulHashIndex = 0;

    if (DOS_ADDR_INVALID(aszNames)
        || DOS_ADDR_INVALID(aszValues))
    {
        DOS_ASSERT(0);
        return DOS_FAIL;
    }

    pszGWGrpID = aszValues[0];
    if (DOS_ADDR_INVALID(pszGWGrpID)
        || dos_atoul(pszGWGrpID, &ulID) < 0)
    {
        DOS_ASSERT(0);

        return DOS_FAIL;
    }

    pstHashNode = dos_dmem_alloc(sizeof(HASH_NODE_S));
    if (DOS_ADDR_INVALID(pstHashNode))
    {
        DOS_ASSERT(0);

        return DOS_FAIL;
    }

    pstGWGrpNode = dos_dmem_alloc(sizeof(SC_GW_GRP_NODE_ST));
    if (DOS_ADDR_INVALID(pstGWGrpNode))
    {
        DOS_ASSERT(0);

        dos_dmem_free(pstHashNode);
        return DOS_FAIL;
    }

    pstGWGrpNode->ulGWGrpID = ulID;
    pstGWGrpNode->bExist = DOS_TRUE;

    HASH_Init_Node(pstHashNode);
    pstHashNode->pHandle = pstGWGrpNode;
    DLL_Init(&pstGWGrpNode->stGWList);
    pthread_mutex_init(&pstGWGrpNode->mutexGWList, NULL);

    ulHashIndex = sc_ep_gw_grp_hash_func(pstGWGrpNode->ulGWGrpID);

    pthread_mutex_lock(&g_mutexHashGWGrp);
    hash_add_node(g_pstHashGWGrp, pstHashNode, ulHashIndex, NULL);
    pthread_mutex_unlock(&g_mutexHashGWGrp);

    return DOS_SUCC;
}

/**
 * 函数: U32 sc_load_did_number()
 * 功能: 加载网关组数据
 * 参数:
 * 返回值: 成功返回DOS_SUCC，否则返回DOS_FAIL
 */
U32 sc_load_gateway_grp(U32 ulIndex)
{
    S8 szSQL[1024];
    U32 ulRet;

    if (SC_INVALID_INDEX == ulIndex)
    {
        dos_snprintf(szSQL, sizeof(szSQL)
                        , "SELECT id FROM tbl_gateway_grp WHERE tbl_gateway_grp.status = 0;");
    }
    else
    {
        dos_snprintf(szSQL, sizeof(szSQL)
                        , "SELECT id FROM tbl_gateway_grp WHERE tbl_gateway_grp.status = 0 AND id = %d;"
                        , ulIndex);
    }

    ulRet = db_query(g_pstSCDBHandle, szSQL, sc_load_gateway_grp_cb, NULL, NULL);
    if (ulRet != DOS_SUCC)
    {
        DOS_ASSERT(0);
        sc_logr_error(SC_FUNC, "Load gateway Group FAIL.(ID:%u)", ulIndex);
        return DOS_FAIL;
    }
    sc_logr_info(SC_FUNC, "Load gateway Group SUCC.(ID:%u)", ulIndex);

    return ulRet;
}

/**
 * 函数: S32 sc_load_relationship_cb(VOID *pArg, S32 lCount, S8 **aszValues, S8 **aszNames)
 * 功能: 加载路由网关组关系数据
 * 参数:
 *      VOID *pArg: 参数
 *      S32 lCount: 列数量
 *      S8 **aszValues: 值裂变
 *      S8 **aszNames: 字段名列表
 * 返回值: 成功返回DOS_SUCC，否则返回DOS_FAIL
 */
S32 sc_load_gw_relationship_cb(VOID *pArg, S32 lCount, S8 **aszValues, S8 **aszNames)
{
    SC_GW_GRP_NODE_ST    *pstGWGrp      = NULL;
    DLL_NODE_S           *pstListNode   = NULL;
    HASH_NODE_S          *pstHashNode   = NULL;
    U32                  ulGatewayID    = U32_BUTT;
    U32                  ulHashIndex    = U32_BUTT;
    S32                  lIndex         = 0;
    BOOL                 blProcessOK    = DOS_TRUE;

    if (DOS_ADDR_INVALID(pArg)
        || lCount <= 0
        || DOS_ADDR_INVALID(aszValues)
        || DOS_ADDR_INVALID(aszNames))
    {
        DOS_ASSERT(0);

        return DOS_FAIL;
    }

    for (lIndex=0; lIndex < lCount; lIndex++)
    {
        if (DOS_ADDR_INVALID(aszNames[lIndex])
            || DOS_ADDR_INVALID(aszValues[lIndex]))
        {
            break;
        }

        if (0 == dos_strncmp(aszNames[lIndex], "gateway_id", dos_strlen("gateway_id")))
        {
            if (dos_atoul(aszValues[lIndex], &ulGatewayID) < 0)
            {
                blProcessOK  = DOS_FALSE;
                break;
            }
        }
    }

    if (!blProcessOK
        || U32_BUTT == ulGatewayID)
    {
        DOS_ASSERT(0);

        return DOS_FAIL;
    }

    ulHashIndex = sc_ep_gw_hash_func(ulGatewayID);
    pstHashNode = hash_find_node(g_pstHashGW, ulHashIndex, &ulGatewayID, sc_ep_gw_hash_find);
    if (DOS_ADDR_INVALID(pstHashNode)
        || DOS_ADDR_INVALID(pstHashNode->pHandle))
    {
        DOS_ASSERT(0);
        return DOS_FAIL;
    }

    pstListNode = dos_dmem_alloc(sizeof(DLL_NODE_S));
    if (DOS_ADDR_INVALID(pstListNode))
    {
        DOS_ASSERT(0);
        return DOS_FAIL;
    }
    DLL_Init_Node(pstListNode);
    pstListNode->pHandle = pstHashNode->pHandle;

    pstGWGrp = (SC_GW_GRP_NODE_ST *)pArg;

    pthread_mutex_lock(&pstGWGrp->mutexGWList);
    DLL_Add(&pstGWGrp->stGWList, pstListNode);
    pthread_mutex_unlock(&pstGWGrp->mutexGWList);

    sc_logr_debug(SC_FUNC, "Gateway %u will be added into group %u.", ulGatewayID, pstGWGrp->ulGWGrpID);

    return DOS_FAIL;
}

/**
 * 函数: U32 sc_load_gw_relationship()
 * 功能: 加载路由网关组关系数据
 * 参数:
 * 返回值: 成功返回DOS_SUCC，否则返回DOS_FAIL
 */
U32 sc_load_gw_relationship()
{
    SC_GW_GRP_NODE_ST    *pstGWGrp      = NULL;
    HASH_NODE_S          *pstHashNode   = NULL;
    U32                  ulHashIndex = 0;
    S8 szSQL[1024] = {0, };

    HASH_Scan_Table(g_pstHashGWGrp, ulHashIndex)
    {
        HASH_Scan_Bucket(g_pstHashGWGrp, ulHashIndex, pstHashNode, HASH_NODE_S *)
        {
            pstGWGrp = pstHashNode->pHandle;
            if (DOS_ADDR_INVALID(pstGWGrp))
            {
                DOS_ASSERT(0);
                continue;
            }

            dos_snprintf(szSQL, sizeof(szSQL), "SELECT gateway_id FROM tbl_gateway_assign WHERE gateway_grp_id=%d;", pstGWGrp->ulGWGrpID);

            db_query(g_pstSCDBHandle, szSQL, sc_load_gw_relationship_cb, (VOID *)pstGWGrp, NULL);
        }
    }

    return DOS_SUCC;
}



/**
 * 函数: S32 sc_load_caller_cb(VOID *pArg, S32 lCount, S8 **aszValues, S8 **aszNames)
 * 功能: 加载主叫号码数据
 * 参数:
 *      VOID *pArg: 参数
 *      S32 lCount: 列数量
 *      S8 **aszValues: 值裂变
 *      S8 **aszNames: 字段名列表
 * 返回值: 成功返回DOS_SUCC，否则返回DOS_FAIL
 */
S32 sc_load_caller_cb(VOID *pArg, S32 lCount, S8 **aszValues, S8 **aszNames)
{
    SC_CALLER_QUERY_NODE_ST  *pstCaller   = NULL, *pstCallerTemp = NULL;
    HASH_NODE_S              *pstHashNode = NULL;
    U32  ulHashIndex = U32_BUTT;
    S32  lIndex = U32_BUTT;
    BOOL blProcessOK = DOS_FALSE;

    if (DOS_ADDR_INVALID(aszValues)
        || DOS_ADDR_INVALID(aszNames))
    {
        DOS_ASSERT(0);
        return DOS_FAIL;
    }

    pstCaller = (SC_CALLER_QUERY_NODE_ST *)dos_dmem_alloc(sizeof(SC_CALLER_QUERY_NODE_ST));
    if (DOS_ADDR_INVALID(pstCaller))
    {
        DOS_ASSERT(0);

        return DOS_FAIL;
    }

    sc_ep_caller_init(pstCaller);

    for (blProcessOK = DOS_TRUE, lIndex = 0; lIndex < lCount; lIndex++)
    {
        if (0 == dos_strnicmp(aszNames[lIndex], "id", dos_strlen("id")))
        {
            if (dos_atoul(aszValues[lIndex], &pstCaller->ulIndexInDB) < 0)
            {
                DOS_ASSERT(0);
                blProcessOK = DOS_FALSE;
                break;
            }
        }
        else if (0 == dos_strnicmp(aszNames[lIndex], "customer_id", dos_strlen("customer_id")))
        {
            if (dos_atoul(aszValues[lIndex], &pstCaller->ulCustomerID) < 0)
            {
                DOS_ASSERT(0);
                blProcessOK = DOS_FALSE;
                break;
            }
        }
        else if (0 == dos_strnicmp(aszNames[lIndex], "cid", dos_strlen("cid")))
        {
            if ('\0' == aszValues[lIndex][0])
            {
                DOS_ASSERT(0);
                blProcessOK = DOS_FALSE;
                break;
            }
            dos_snprintf(pstCaller->szNumber, sizeof(pstCaller->szNumber), "%s", aszValues[lIndex]);
        }
        else
        {
            DOS_ASSERT(0);
            blProcessOK = DOS_FALSE;
            break;
        }
        blProcessOK = DOS_TRUE;
    }
    if (!blProcessOK)
    {
        DOS_ASSERT(0);
        dos_dmem_free(pstCaller);
        pstCaller = NULL;
        return DOS_FAIL;
    }

    pthread_mutex_lock(&g_mutexHashCaller);
    ulHashIndex = sc_ep_caller_hash_func(pstCaller->ulIndexInDB);
    pstHashNode = hash_find_node(g_pstHashCaller, ulHashIndex, (VOID *)&pstCaller->ulIndexInDB, sc_ep_caller_hash_find);
    if (DOS_ADDR_INVALID(pstHashNode))
    {
        pstHashNode = (HASH_NODE_S*)dos_dmem_alloc(sizeof(HASH_NODE_S));
        if (DOS_ADDR_INVALID(pstHashNode))
        {
            DOS_ASSERT(0);
            dos_dmem_free(pstCaller);
            pstCaller = NULL;
            pthread_mutex_unlock(&g_mutexHashCaller);
            return DOS_FAIL;
        }

        HASH_Init_Node(pstHashNode);
        pstHashNode->pHandle = (VOID *)pstCaller;
        hash_add_node(g_pstHashCaller, pstHashNode, ulHashIndex, NULL);
    }
    else
    {
        pstCallerTemp = (SC_CALLER_QUERY_NODE_ST *)pstHashNode->pHandle;
        if (DOS_ADDR_INVALID(pstCallerTemp))
        {
            DOS_ASSERT(0);
            dos_dmem_free(pstCaller);
            pstCaller = NULL;
            pthread_mutex_unlock(&g_mutexHashCaller);
            return DOS_FAIL;
        }

        dos_memcpy(pstCallerTemp, pstCaller, sizeof(SC_CALLER_QUERY_NODE_ST));
        dos_dmem_free(pstCaller);
        pstCaller = NULL;
    }
    pthread_mutex_unlock(&g_mutexHashCaller);

    return DOS_SUCC;
}


/**
 * 函数: U32 sc_load_caller(U32 ulIndex)
 * 功能: 加载主叫号码数据
 * 参数:
 * 返回值: 成功返回DOS_SUCC，否则返回DOS_FAIL
 */
U32 sc_load_caller(U32 ulIndex)
{
    S8   szQuery[256] = {0};
    U32  ulRet = U32_BUTT;

    if (SC_INVALID_INDEX == ulIndex)
    {
        dos_snprintf(szQuery, sizeof(szQuery), "SELECT id,customer_id,cid FROM tbl_caller WHERE verify_status=1;");
    }
    else
    {
        dos_snprintf(szQuery, sizeof(szQuery), "SELECT id,customer_id,cid FROM tbl_caller WHERE verify_status=1 AND id=%u", ulIndex);
    }
    ulRet = db_query(g_pstSCDBHandle, szQuery, sc_load_caller_cb, NULL, NULL);
    if (DB_ERR_SUCC != ulRet)
    {
        sc_logr_error(SC_FUNC, "Load caller FAIL.(ID:%u)", ulIndex);
        DOS_ASSERT(0);
        return DOS_FAIL;
    }
    sc_logr_info(SC_FUNC, "Load Caller SUCC.(ID:%u)", ulIndex);

    return DOS_SUCC;
}


/**
 * 函数: S32 sc_load_caller_grp_cb(VOID *pArg, S32 lCount, S8 **aszValues, S8 **aszNames)
 * 功能: 加载主叫号码数据
 * 参数:
 *      VOID *pArg: 参数
 *      S32 lCount: 列数量
 *      S8 **aszValues: 值裂变
 *      S8 **aszNames: 字段名列表
 * 返回值: 成功返回DOS_SUCC，否则返回DOS_FAIL
 */
S32 sc_load_caller_grp_cb(VOID *pArg, S32 lCount, S8 **aszValues, S8 **aszNames)
{
    SC_CALLER_GRP_NODE_ST   *pstCallerGrp = NULL, *pstCallerGrpTemp = NULL;
    HASH_NODE_S *pstHashNode = NULL;
    U32  ulHashIndex = U32_BUTT;
    S32  lIndex = U32_BUTT;
    BOOL blProcessOK = DOS_FALSE;

    if (DOS_ADDR_INVALID(aszValues)
            || DOS_ADDR_INVALID(aszNames))
    {
        DOS_ASSERT(0);
        return DOS_FAIL;
    }

    pstCallerGrp = (SC_CALLER_GRP_NODE_ST *)dos_dmem_alloc(sizeof(SC_CALLER_GRP_NODE_ST));
    if (DOS_ADDR_INVALID(pstCallerGrp))
    {
        DOS_ASSERT(0);

        return DOS_FAIL;
    }

    sc_ep_caller_grp_init(pstCallerGrp);

    for (blProcessOK = DOS_TRUE, lIndex = 0; lIndex < lCount; lIndex++)
    {
        if (0 == dos_strnicmp(aszNames[lIndex], "id", dos_strlen("id")))
        {
            if (dos_atoul(aszValues[lIndex], &pstCallerGrp->ulID) < 0)
            {
                DOS_ASSERT(0);
                blProcessOK = DOS_FALSE;
                break;
            }
        }
        else if (0 == dos_strnicmp(aszNames[lIndex], "customer_id", dos_strlen("customer_id")))
        {
            if (dos_atoul(aszValues[lIndex], &pstCallerGrp->ulCustomerID) < 0)
            {
                DOS_ASSERT(0);
                blProcessOK = DOS_FALSE;
                break;
            }
        }
        else if (0 == dos_strnicmp(aszNames[lIndex], "name", dos_strlen("name")))
        {
            if ('\0' == aszValues[lIndex][0])
            {
                DOS_ASSERT(0);
                blProcessOK = DOS_FALSE;
                break;
            }
            dos_snprintf(pstCallerGrp->szGrpName, sizeof(pstCallerGrp->szGrpName), "%s", aszValues[lIndex]);
        }
        else if (0 == dos_strnicmp(aszNames[lIndex], "policy", dos_strlen("policy")))
        {
            if (dos_atoul(aszValues[lIndex], &pstCallerGrp->ulPolicy) < 0)
            {
                DOS_ASSERT(0);
                blProcessOK = DOS_FALSE;
                break;
            }
        }
        else if (0 == dos_strnicmp(aszNames[lIndex], "is_default", dos_strlen("is_default")))
        {
            if (dos_atoul(aszValues[lIndex], &pstCallerGrp->bDefault) < 0)
            {
                DOS_ASSERT(0);
                blProcessOK = DOS_FALSE;
                break;
            }
        }
        else
        {
            DOS_ASSERT(0);
            blProcessOK = DOS_FALSE;
            break;
        }
    }

    pthread_mutex_lock(&g_mutexHashCallerGrp);
    ulHashIndex = sc_ep_caller_grp_hash_func(pstCallerGrp->ulID);
    pstHashNode = hash_find_node(g_pstHashCallerGrp, ulHashIndex, (VOID *)&pstCallerGrp->ulID, sc_ep_caller_grp_hash_find);
    if (DOS_ADDR_INVALID(pstHashNode))
    {
        pstHashNode = (HASH_NODE_S *)dos_dmem_alloc(sizeof(HASH_NODE_S));
        if (DOS_ADDR_INVALID(pstHashNode))
        {
            DOS_ASSERT(0);
            dos_dmem_free(pstCallerGrp);
            pstCallerGrp = NULL;
            pthread_mutex_unlock(&g_mutexHashCallerGrp);
            return DOS_FAIL;
        }

        HASH_Init_Node(pstHashNode);
        pstHashNode->pHandle = (VOID *)pstCallerGrp;
        hash_add_node(g_pstHashCallerGrp, pstHashNode, ulHashIndex, NULL);
    }
    else
    {
        pstCallerGrpTemp = (SC_CALLER_GRP_NODE_ST *)pstHashNode->pHandle;
        if (DOS_ADDR_INVALID(pstCallerGrpTemp))
        {
            DOS_ASSERT(0);
            dos_dmem_free(pstCallerGrp);
            pstCallerGrp = NULL;
            pthread_mutex_unlock(&g_mutexHashCallerGrp);
            return DOS_FAIL;
        }
        dos_memcpy(pstCallerGrpTemp, pstCallerGrp, sizeof(SC_CALLER_QUERY_NODE_ST));
        dos_dmem_free(pstCallerGrp);
        pstCallerGrp = NULL;
    }
    pthread_mutex_unlock(&g_mutexHashCallerGrp);

    return DOS_SUCC;
}

/**
 * 函数: U32 sc_load_caller_grp(U32 ulIndex)
 * 功能: 加载主叫号码组数据
 * 参数:
 * 返回值: 成功返回DOS_SUCC，否则返回DOS_FAIL
 */
U32 sc_load_caller_grp(U32 ulIndex)
{
    S8 szSQL[1024] = {0};
    U32 ulRet = U32_BUTT;

    if (SC_INVALID_INDEX == ulIndex)
    {
        dos_snprintf(szSQL, sizeof(szSQL)
                        , "SELECT id,customer_id,name,policy,is_default FROM tbl_caller_grp;");
    }
    else
    {
        dos_snprintf(szSQL, sizeof(szSQL)
                        , "SELECT id,customer_id,name,policy,is_default FROM tbl_caller_grp WHERE id=%u;"
                        , ulIndex);
    }

    ulRet = db_query(g_pstSCDBHandle, szSQL, sc_load_caller_grp_cb, NULL, NULL);
    if (ulRet != DOS_SUCC)
    {
        sc_logr_error(SC_FUNC, "Load caller group FAIL.(ID:%u)", ulIndex);
        DOS_ASSERT(0);
        return DOS_FAIL;
    }
    sc_logr_info(SC_FUNC, "Load caller group SUCC.(ID:%u)", ulIndex);

    return ulRet;
}


S32 sc_load_caller_relationship_cb(VOID *pArg, S32 lCount, S8 **aszValues, S8 **aszNames)
{
    SC_CALLER_GRP_NODE_ST *pstCallerGrp = NULL;
    S32  lIndex = U32_BUTT;
    U32  ulCallerID = U32_BUTT, ulCustomerID = U32_BUTT, ulCallerType = U32_BUTT, ulHashIndex = U32_BUTT;
    SC_DID_NODE_ST *pstDid = NULL;
    DLL_NODE_S *pstNode = NULL;
    SC_CALLER_QUERY_NODE_ST *pstCaller = NULL;
    HASH_NODE_S *pstHashNode = NULL;
    SC_CALLER_CACHE_NODE_ST *pstCacheNode = NULL;
    BOOL blProcessOK = DOS_TRUE, bFound = DOS_FALSE;

    if (DOS_ADDR_INVALID(pArg)
        || lCount <= 0
        || DOS_ADDR_INVALID(aszValues)
        || DOS_ADDR_INVALID(aszNames))
    {
        DOS_ASSERT(0);
        return DOS_FAIL;
    }

    for (lIndex = 0; lIndex < lCount; lIndex++)
    {
        if (DOS_ADDR_INVALID(aszNames[lIndex])
            || DOS_ADDR_INVALID(aszValues[lIndex]))
        {
            break;
        }

        if (0 == dos_strnicmp(aszNames[lIndex], "caller_id", dos_strlen("caller_id")))
        {
            if (dos_atoul(aszValues[lIndex], &ulCallerID) < 0)
            {
                blProcessOK = DOS_FALSE;
                DOS_ASSERT(0);
                break;
            }
        }
        else if (0 == dos_strnicmp(aszNames[lIndex], "customer_id", dos_strlen("customer_id")))
        {
            if (dos_atoul(aszValues[lIndex], &ulCustomerID) < 0)
            {
                blProcessOK = DOS_FALSE;
                DOS_ASSERT(0);
                break;
            }
        }
        else if (0 == dos_strnicmp(aszNames[lIndex], "caller_type", dos_strlen("caller_type")))
        {
            if (dos_atoul(aszValues[lIndex], &ulCallerType) < 0)
            {
                blProcessOK = DOS_FALSE;
            }
        }
    }

    if (!blProcessOK
        || U32_BUTT == ulCallerID)
    {
        DOS_ASSERT(0);
        return DOS_FAIL;
    }

    pstCacheNode = (SC_CALLER_CACHE_NODE_ST *)dos_dmem_alloc(sizeof(SC_CALLER_CACHE_NODE_ST));
    if (DOS_ADDR_INVALID(pstCacheNode))
    {
        DOS_ASSERT(0);
        return DOS_FAIL;
    }

    /* 如果是配置的主叫号码，先查找到对应的主叫号码节点 */
    if (SC_NUMBER_TYPE_CFG == ulCallerType)
    {
        pstCacheNode->ulType = SC_NUMBER_TYPE_CFG;
        ulHashIndex = sc_ep_caller_hash_func(ulCallerID);
        pstHashNode = hash_find_node(g_pstHashCaller, ulHashIndex, (VOID *)&ulCallerID, sc_ep_caller_hash_find);

        if (DOS_ADDR_INVALID(pstHashNode)
            || DOS_ADDR_INVALID(pstHashNode->pHandle))
        {
            DOS_ASSERT(0);
            return DOS_FAIL;
        }
        pstCaller = (SC_CALLER_QUERY_NODE_ST *)pstHashNode->pHandle;
        pstCacheNode->stData.pstCaller = pstCaller;
    }
    else if (SC_NUMBER_TYPE_DID == ulCallerType)
    {
        pstCacheNode->ulType = SC_NUMBER_TYPE_DID;
        /* 目前由于索引未采用id，故只能遍历哈希表 */
        HASH_Scan_Table(g_pstHashDIDNum, ulHashIndex)
        {
            HASH_Scan_Bucket(g_pstHashDIDNum ,ulHashIndex ,pstHashNode, HASH_NODE_S *)
            {
                if (DOS_ADDR_INVALID(pstHashNode)
                    || DOS_ADDR_INVALID(pstHashNode->pHandle))
                {
                    continue;
                }

                pstDid = (SC_DID_NODE_ST *)pstHashNode->pHandle;
                if (ulCallerID == pstDid->ulDIDID)
                {
                    bFound = DOS_TRUE;
                    break;
                }
            }
            if (DOS_FALSE == bFound)
            {
                break;
            }
        }
        if (DOS_FALSE == bFound)
        {
            DOS_ASSERT(0);
            return DOS_FAIL;
        }
        pstCacheNode->stData.pstDid = pstDid;
    }

    pstNode = (DLL_NODE_S *)dos_dmem_alloc(sizeof(DLL_NODE_S));
    if (DOS_ADDR_INVALID(pstNode))
    {
        DOS_ASSERT(0);
        return DOS_FAIL;
    }

    DLL_Init_Node(pstNode);
    pstNode->pHandle = (VOID *)pstCacheNode;

    pstCallerGrp = (SC_CALLER_GRP_NODE_ST *)pArg;
    pthread_mutex_lock(&pstCallerGrp->mutexCallerList);
    DLL_Add(&pstCallerGrp->stCallerList, pstNode);
    pthread_mutex_unlock(&pstCallerGrp->mutexCallerList);
    return DOS_SUCC;
}

U32 sc_load_caller_relationship()
{
    SC_CALLER_GRP_NODE_ST *pstCallerGrp = NULL;
    U32  ulHashIndex = U32_BUTT, ulRet = U32_BUTT;
    HASH_NODE_S *pstHashNode = NULL;

    HASH_Scan_Table(g_pstHashCallerGrp, ulHashIndex)
    {
        HASH_Scan_Bucket(g_pstHashCallerGrp, ulHashIndex, pstHashNode, HASH_NODE_S *)
        {
            if (DOS_ADDR_INVALID(pstHashNode)
                || DOS_ADDR_INVALID(pstHashNode->pHandle))
            {
                continue;
            }
            pstCallerGrp = (SC_CALLER_GRP_NODE_ST *)pstHashNode->pHandle;
            ulRet = sc_refresh_gateway_grp(pstCallerGrp->ulID);
            if (DOS_SUCC != ulRet)
            {
                sc_logr_debug(SC_FUNC, "Refresh gwgrp %u FAIL.", pstCallerGrp->ulID);
            }
#if 0
            dos_snprintf(szQuery, sizeof(szQuery), "SELECT caller_id,customer_id, caller_type FROM tbl_caller_assign WHERE caller_grp_id=%u;", pstCallerGrp->ulID);

            lRet = db_query(g_pstSCDBHandle, szQuery, sc_load_caller_relationship_cb, (VOID *)pstCallerGrp, NULL);
            if (DB_ERR_SUCC != lRet)
            {
                sc_logr_error(SC_FUNC, "%s", "Load caller relationship FAIL.");
                DOS_ASSERT(0);
                return DOS_FAIL;
            }
#endif
        }
    }
    sc_logr_info(SC_FUNC, "%s", "Load caller relationship SUCC.");

    return DOS_SUCC;
}

S32 sc_load_caller_setting_cb(VOID *pArg, S32 lCount, S8 **aszValues, S8 **aszNames)
{
    S32  lIndex = U32_BUTT;
    U32  ulHashIndex = U32_BUTT;
    HASH_NODE_S *pstHashNode = NULL;
    SC_CALLER_SETTING_ST *pstSetting = NULL, *pstSettingTemp = NULL;
    BOOL  blProcessOK = DOS_FALSE;

    if (DOS_ADDR_INVALID(aszValues)
        || DOS_ADDR_INVALID(aszNames))
    {
        DOS_ASSERT(0);
        return DOS_FAIL;
    }

    pstSetting = (SC_CALLER_SETTING_ST *)dos_dmem_alloc(sizeof(SC_CALLER_SETTING_ST));
    if (DOS_ADDR_INVALID(pstSetting))
    {
        DOS_ASSERT(0);
        return DOS_FAIL;
    }

    sc_ep_caller_setting_init(pstSetting);

    for (lIndex = 0; lIndex < lCount; ++lIndex)
    {
        if (0 == dos_strnicmp(aszNames[lIndex], "id", dos_strlen("id")))
        {
            if (dos_atoul(aszValues[lIndex], &pstSetting->ulID) < 0)
            {
                blProcessOK = DOS_FALSE;
                DOS_ASSERT(0);
                break;
            }
        }
        else if (0 == dos_strnicmp(aszNames[lIndex], "customer_id", dos_strlen("customer_id")))
        {
            if (dos_atoul(aszValues[lIndex], &pstSetting->ulCustomerID) < 0)
            {
                blProcessOK = DOS_FALSE;
                DOS_ASSERT(0);
                break;
            }
        }
        else if (0 == dos_strnicmp(aszNames[lIndex], "name", dos_strlen("name")))
        {
            if ('\0' == aszValues[lIndex][0])
            {
                blProcessOK = DOS_FALSE;
                DOS_ASSERT(0);
                break;
            }
            dos_snprintf(pstSetting->szSettingName, sizeof(pstSetting->szSettingName), "%s", aszValues[lIndex]);
        }
        else if (0 == dos_strnicmp(aszNames[lIndex], "src_id", dos_strlen("src_id")))
        {
            if (dos_atoul(aszValues[lIndex], &pstSetting->ulSrcID) < 0)
            {
                blProcessOK = DOS_FALSE;
                DOS_ASSERT(0);
                break;
            }
        }
        else if (0 == dos_strnicmp(aszNames[lIndex], "src_type", dos_strlen("src_type")))
        {
            if (dos_atoul(aszValues[lIndex], &pstSetting->ulSrcType) < 0)
            {
                blProcessOK = DOS_FALSE;
                DOS_ASSERT(0);
                break;
            }
        }
        else if (0 == dos_strnicmp(aszNames[lIndex], "dest_id", dos_strlen("dest_id")))
        {
            if (dos_atoul(aszValues[lIndex], &pstSetting->ulDstID) < 0)
            {
                blProcessOK = DOS_FALSE;
                DOS_ASSERT(0);
                break;
            }
        }
        else if (0 == dos_strnicmp(aszNames[lIndex], "dest_type", dos_strlen("dest_type")))
        {
            if (dos_atoul(aszValues[lIndex], &pstSetting->ulDstType) < 0)
            {
                blProcessOK = DOS_FALSE;
                DOS_ASSERT(0);
                break;
            }
        }
        else
        {
            blProcessOK = DOS_FALSE;
            DOS_ASSERT(0);
            break;
        }
        blProcessOK = DOS_TRUE;
    }
    if (!blProcessOK)
    {
        dos_dmem_free(pstSetting);
        pstSetting = NULL;
        DOS_ASSERT(0);
        return DOS_FAIL;
    }

    ulHashIndex = sc_ep_caller_setting_hash_func(pstSetting->ulID);
    pstHashNode = hash_find_node(g_pstHashCallerSetting, ulHashIndex, (VOID *)&pstSetting->ulID, sc_ep_caller_setting_hash_find);
    pthread_mutex_lock(&g_mutexHashCallerSetting);
    if (DOS_ADDR_INVALID(pstHashNode))
    {
        pstHashNode = (HASH_NODE_S *)dos_dmem_alloc(sizeof(HASH_NODE_S));
        if (DOS_ADDR_INVALID(pstHashNode))
        {
            DOS_ASSERT(0);
            pthread_mutex_unlock(&g_mutexHashCallerSetting);
            dos_dmem_free(pstSetting);
            pstSetting = NULL;
            return DOS_FAIL;
        }
        DLL_Init_Node(pstHashNode);
        pstHashNode->pHandle = (VOID *)pstSetting;
        hash_add_node(g_pstHashCallerSetting, pstHashNode, ulHashIndex, NULL);
    }
    else
    {
        pstSettingTemp = (SC_CALLER_SETTING_ST *)pstHashNode->pHandle;
        if (DOS_ADDR_INVALID(pstSettingTemp))
        {
            dos_dmem_free(pstSetting);
            pstSetting = NULL;
            DOS_ASSERT(0);
            pthread_mutex_unlock(&g_mutexHashCallerSetting);
        }
        dos_memcpy(pstSettingTemp, pstSetting, sizeof(SC_CALLER_SETTING_ST));
        dos_dmem_free(pstSetting);
        pstSetting = NULL;
    }
    pthread_mutex_unlock(&g_mutexHashCallerSetting);

    return DOS_SUCC;
}

U32  sc_load_caller_setting(U32 ulIndex)
{
    S8  szQuery[128] = {0};
    S32 lRet;

    if (SC_INVALID_INDEX == ulIndex)
    {
        dos_snprintf(szQuery, sizeof(szQuery), "SELECT * FROM tbl_caller_setting;");
    }
    else
    {
        dos_snprintf(szQuery, sizeof(szQuery), "SELECT * FROM tbl_caller_setting WHERE id=%u;", ulIndex);
    }

    lRet = db_query(g_pstSCDBHandle, szQuery, sc_load_caller_setting_cb, NULL, NULL);
    if (DB_ERR_SUCC != lRet)
    {
        sc_logr_error(SC_FUNC, "Load caller setting FAIL.(ID:%u)", ulIndex);
        DOS_ASSERT(0);
        return DOS_FAIL;
    }
    sc_logr_info(SC_FUNC, "Load caller setting SUCC.(ID:%u)", ulIndex);

    return DOS_SUCC;
}

U32 sc_refresh_gateway_grp(U32 ulIndex)
{
    S8 szSQL[1024];
    U32 ulHashIndex;
    SC_GW_GRP_NODE_ST    *pstGWGrpNode  = NULL;
    SC_GW_NODE_ST        *pstGWNode = NULL;
    HASH_NODE_S          *pstHashNode   = NULL;
    DLL_NODE_S           *pstDLLNode    = NULL;

    if (SC_INVALID_INDEX == ulIndex || U32_BUTT == ulIndex)
    {
        DOS_ASSERT(0);
        return DOS_FAIL;
    }

    ulHashIndex = sc_ep_gw_grp_hash_func(ulIndex);
    pstHashNode = hash_find_node(g_pstHashGWGrp, ulHashIndex, &ulIndex, sc_ep_gw_grp_hash_find);
    if (DOS_ADDR_INVALID(pstHashNode) || DOS_ADDR_INVALID(pstHashNode->pHandle))
    {
        DOS_ASSERT(0);
        return DOS_FAIL;
    }

    pstGWGrpNode = pstHashNode->pHandle;
    pthread_mutex_lock(&pstGWGrpNode->mutexGWList);
    while (1)
    {
        if (DLL_Count(&pstGWGrpNode->stGWList) == 0)
        {
            break;
        }

        pstDLLNode = dll_fetch(&pstGWGrpNode->stGWList);
        if (DOS_ADDR_INVALID(pstDLLNode))
        {
            DOS_ASSERT(0);
            break;
        }
        pstGWNode = (SC_GW_NODE_ST *)pstDLLNode->pHandle;
        sc_logr_debug(SC_FUNC, "Gateway %u will be removed from Group %u", pstGWNode->ulGWID, ulIndex);

        /* 此处不能释放网关数据，只需释放链表结点即可，因为一个网关可以属于多个网关组，此处删除，容易产生double free信号 */
        if (DOS_ADDR_VALID(pstDLLNode->pHandle))
        {
            /*dos_dmem_free(pstDLLNode->pHandle);*/
            pstDLLNode->pHandle = NULL;
        }

        dos_dmem_free(pstDLLNode);
        pstDLLNode = NULL;
    }
    pthread_mutex_unlock(&pstGWGrpNode->mutexGWList);

    dos_snprintf(szSQL, sizeof(szSQL)
                        , "SELECT tbl_gateway_assign.gateway_id FROM tbl_gateway_assign WHERE tbl_gateway_assign.gateway_grp_id = %u;"
                        , ulIndex);

    return db_query(g_pstSCDBHandle, szSQL, sc_load_gw_relationship_cb, pstGWGrpNode, NULL);
}


/**
 * 函数: S32 sc_load_route_group_cb(VOID *pArg, S32 lCount, S8 **aszValues, S8 **aszNames)
 * 功能: 加载路由数据时数据库查询的回调函数，将数据加入路由列表中
 * 参数:
 *      VOID *pArg: 参数
 *      S32 lCount: 列数量
 *      S8 **aszValues: 值裂变
 *      S8 **aszNames: 字段名列表
 * 返回值: 成功返回DOS_SUCC，否则返回DOS_FAIL
 */
S32 sc_load_route_cb(VOID *pArg, S32 lCount, S8 **aszValues, S8 **aszNames)
{
    SC_ROUTE_NODE_ST     *pstRoute      = NULL;
    SC_ROUTE_NODE_ST     *pstRouteTmp   = NULL;
    DLL_NODE_S           *pstListNode   = NULL;
    S32                  lIndex;
    S32                  lSecond;
    S32                  lRet;
    S32                  lDestIDCount;
    BOOL                 blProcessOK = DOS_FALSE;
    U32                  ulCallOutGroup;

    if (DOS_ADDR_INVALID(aszValues)
        || DOS_ADDR_INVALID(aszNames))
    {
        DOS_ASSERT(0);

        return DOS_FAIL;
    }

    pstRoute = dos_dmem_alloc(sizeof(SC_ROUTE_NODE_ST));
    if (DOS_ADDR_INVALID(pstRoute))
    {
        DOS_ASSERT(0);

        return DOS_FAIL;
    }

    sc_ep_route_init(pstRoute);


    for (blProcessOK = DOS_TRUE, lIndex = 0; lIndex < lCount; lIndex++)
    {
        if (0 == dos_strnicmp(aszNames[lIndex], "id", dos_strlen("id")))
        {
            if (dos_atoul(aszValues[lIndex], &pstRoute->ulID) < 0)
            {
                DOS_ASSERT(0);

                blProcessOK = DOS_FALSE;
                break;
            }
        }
        else if (0 == dos_strnicmp(aszNames[lIndex], "start_time", dos_strlen("start_time")))
        {
            lRet = dos_sscanf(aszValues[lIndex]
                        , "%d:%d:%d"
                        , &pstRoute->ucHourBegin
                        , &pstRoute->ucMinuteBegin
                        , &lSecond);
            if (3 != lRet)
            {
                DOS_ASSERT(0);

                blProcessOK = DOS_FALSE;
                break;
            }
        }
        else if (0 == dos_strnicmp(aszNames[lIndex], "end_time", dos_strlen("end_time")))
        {
            lRet = dos_sscanf(aszValues[lIndex]
                    , "%d:%d:%d"
                    , &pstRoute->ucHourEnd
                    , &pstRoute->ucMinuteEnd
                    , &lSecond);
            if (3 != lRet)
            {
                DOS_ASSERT(0);

                blProcessOK = DOS_FALSE;
                break;
            }
        }
        else if (0 == dos_strnicmp(aszNames[lIndex], "callee_prefix", dos_strlen("callee_prefix")))
        {
            if (aszValues[lIndex] && '\0' != aszValues[lIndex][0])
            {
                dos_strncpy(pstRoute->szCalleePrefix, aszValues[lIndex], sizeof(pstRoute->szCalleePrefix));
                pstRoute->szCalleePrefix[sizeof(pstRoute->szCalleePrefix) - 1] = '\0';
            }
            else
            {
                pstRoute->szCalleePrefix[0] = '\0';
            }
        }
        else if (0 == dos_strnicmp(aszNames[lIndex], "caller_prefix", dos_strlen("caller_prefix")))
        {
            if (aszValues[lIndex] && '\0' != aszValues[lIndex][0])
            {
                dos_strncpy(pstRoute->szCallerPrefix, aszValues[lIndex], sizeof(pstRoute->szCallerPrefix));
                pstRoute->szCallerPrefix[sizeof(pstRoute->szCallerPrefix) - 1] = '\0';
            }
            else
            {
                pstRoute->szCallerPrefix[0] = '\0';
            }
        }
        else if (0 == dos_strnicmp(aszNames[lIndex], "dest_type", dos_strlen("dest_type")))
        {
            if (DOS_ADDR_INVALID(aszValues[lIndex])
                || '\0' == aszValues[lIndex][0]
                || dos_atoul(aszValues[lIndex], &pstRoute->ulDestType) < 0)
            {
                DOS_ASSERT(0);

                blProcessOK = DOS_FALSE;
                break;
            }
        }
        else if (0 == dos_strnicmp(aszNames[lIndex], "dest_id", dos_strlen("dest_id")))
        {
            if (SC_DEST_TYPE_GATEWAY == pstRoute->ulDestType)
            {
                if (DOS_ADDR_INVALID(aszValues[lIndex])
                    || '\0' == aszValues[lIndex][0]
                    || dos_atoul(aszValues[lIndex], &pstRoute->aulDestID[0]) < 0)
                {
                    DOS_ASSERT(0);

                    blProcessOK = DOS_FALSE;
                    break;
                }
            }
            else if (SC_DEST_TYPE_GW_GRP == pstRoute->ulDestType)
            {
                if (DOS_ADDR_INVALID(aszValues[lIndex])
                    || '\0' == aszValues[lIndex])
                {
                    DOS_ASSERT(0);

                    blProcessOK = DOS_FALSE;
                    break;
                }

                pstRoute->aulDestID[0] = U32_BUTT;
                pstRoute->aulDestID[1] = U32_BUTT;
                pstRoute->aulDestID[2] = U32_BUTT;
                pstRoute->aulDestID[3] = U32_BUTT;
                pstRoute->aulDestID[4] = U32_BUTT;

                lDestIDCount = dos_sscanf(aszValues[lIndex], "%d,%d,%d,%d,%d", &pstRoute->aulDestID[0], &pstRoute->aulDestID[1], &pstRoute->aulDestID[2], &pstRoute->aulDestID[3], &pstRoute->aulDestID[4]);
                if (lDestIDCount < 1)
                {
                    DOS_ASSERT(0);

                    blProcessOK = DOS_FALSE;
                    break;
                }
            }
            else
            {
                DOS_ASSERT(0);

                blProcessOK = DOS_FALSE;
                break;
            }
        }
        else if (0 == dos_strnicmp(aszNames[lIndex], "call_out_group", dos_strlen("call_out_group")))
        {
            if (DOS_ADDR_INVALID(aszValues[lIndex])
                || '\0' == aszValues[lIndex][0]
                || dos_atoul(aszValues[lIndex], &ulCallOutGroup) < 0
                || ulCallOutGroup > U16_BUTT)
            {
                DOS_ASSERT(0);

                blProcessOK = DOS_FALSE;
                break;
            }
            pstRoute->usCallOutGroup = (U16)ulCallOutGroup;
        }
        else if (0 == dos_strnicmp(aszNames[lIndex], "status", dos_strlen("status")))
        {
            if (DOS_ADDR_INVALID(aszValues[lIndex])
                || '\0' == aszValues[lIndex][0]
                || dos_atoul(aszValues[lIndex], &pstRoute->bStatus) < 0)
            {
                DOS_ASSERT(0);
                blProcessOK = DOS_FALSE;
                break;
            }
        }
        else if (0 == dos_strnicmp(aszNames[lIndex], "seq", dos_strlen("seq")))
        {
            if (DOS_ADDR_INVALID(aszValues[lIndex])
                || '\0' == aszValues[lIndex][0]
                || dos_atoul(aszValues[lIndex], (U32 *)&pstRoute->ucPriority) < 0)
            {
                DOS_ASSERT(0);
                blProcessOK = DOS_FALSE;
                break;
            }
        }
    }

    if (!blProcessOK)
    {
        DOS_ASSERT(0);
        dos_dmem_free(pstRoute);
        pstRoute = NULL;
        return DOS_FAIL;
    }

    pthread_mutex_lock(&g_mutexRouteList);
    pstListNode = dll_find(&g_stRouteList, &pstRoute->ulID, sc_ep_route_find);
    if (DOS_ADDR_INVALID(pstListNode))
    {
        pstListNode = (DLL_NODE_S *)dos_dmem_alloc(sizeof(DLL_NODE_S));
        if (DOS_ADDR_INVALID(pstListNode))
        {
            DOS_ASSERT(0);

            dos_dmem_free(pstRoute);
            pstRoute = NULL;

            pthread_mutex_unlock(&g_mutexRouteList);
            return DOS_FAIL;
        }

        DLL_Init_Node(pstListNode);
        pstListNode->pHandle = pstRoute;
        pstRoute->bExist = DOS_TRUE;
        DLL_Add(&g_stRouteList, pstListNode);
    }
    else
    {
        pstRouteTmp = pstListNode->pHandle;
        if (DOS_ADDR_INVALID(pstRouteTmp))
        {
            DOS_ASSERT(0);

            dos_dmem_free(pstRoute);
            pstRoute = NULL;

            pthread_mutex_unlock(&g_mutexRouteList);
            return DOS_FAIL;
        }

        dos_memcpy(pstRouteTmp, pstRoute, sizeof(SC_ROUTE_NODE_ST));
        pstRouteTmp->bExist = DOS_TRUE;

        dos_dmem_free(pstRoute);
        pstRoute = NULL;
    }
    pthread_mutex_unlock(&g_mutexRouteList);

    return DOS_TRUE;
}

/**
 * 函数: U32 sc_load_route()
 * 功能: 加载路由数据
 * 参数:
 * 返回值: 成功返回DOS_SUCC，否则返回DOS_FAIL
 */
U32 sc_load_route(U32 ulIndex)
{
    S8 szSQL[1024];
    S32 lRet;

    if (SC_INVALID_INDEX == ulIndex)
    {
        dos_snprintf(szSQL, sizeof(szSQL)
                    , "SELECT id, start_time, end_time, callee_prefix, caller_prefix, dest_type, dest_id, call_out_group, status, seq FROM tbl_route ORDER BY tbl_route.seq ASC;");
    }
    else
    {
        dos_snprintf(szSQL, sizeof(szSQL)
                    , "SELECT id, start_time, end_time, callee_prefix, caller_prefix, dest_type, dest_id, call_out_group, status, seq FROM tbl_route WHERE id=%d ORDER BY tbl_route.seq ASC;"
                    , ulIndex);
    }

    lRet = db_query(g_pstSCDBHandle, szSQL, sc_load_route_cb, NULL, NULL);
    if (DB_ERR_SUCC != lRet)
    {
        DOS_ASSERT(0);
        sc_logr_error(SC_FUNC, "Load route FAIL(ID:%u).", ulIndex);
        return DOS_FAIL;
    }
    sc_logr_info(SC_FUNC, "Load route SUCC.(ID:%u)", ulIndex);

    return DOS_SUCC;
}

S32 sc_load_num_transform_cb(VOID *pArg, S32 lCount, S8 **aszValues, S8 **aszNames)
{
    SC_NUM_TRANSFORM_NODE_ST    *pstNumTransform    = NULL;
    SC_NUM_TRANSFORM_NODE_ST    *pstNumTransformTmp = NULL;
    DLL_NODE_S                  *pstListNode        = NULL;
    S32                         lIndex              = 0;
    S32                         lRet                = 0;
    BOOL                        blProcessOK         = DOS_FALSE;

    if (DOS_ADDR_INVALID(aszValues)
        || DOS_ADDR_INVALID(aszNames))
    {
        DOS_ASSERT(0);

        return DOS_FAIL;
    }

    pstNumTransform = (SC_NUM_TRANSFORM_NODE_ST *)dos_dmem_alloc(sizeof(SC_NUM_TRANSFORM_NODE_ST));
    if (DOS_ADDR_INVALID(pstNumTransform))
    {
        DOS_ASSERT(0);

        return DOS_FAIL;
    }
    sc_ep_num_transform_init(pstNumTransform);

    for (blProcessOK = DOS_TRUE, lIndex = 0; lIndex < lCount; lIndex++)
    {
        if (0 == dos_strnicmp(aszNames[lIndex], "id", dos_strlen("id")))
        {
            if (dos_atoul(aszValues[lIndex], &pstNumTransform->ulID) < 0)
            {
                DOS_ASSERT(0);

                blProcessOK = DOS_FALSE;
                break;
            }
        }
        else if (0 == dos_strnicmp(aszNames[lIndex], "object_id", dos_strlen("object_id")))
        {
            if ('\0' == aszValues[lIndex][0])
            {
                continue;
            }

            lRet = dos_atoul(aszValues[lIndex], &pstNumTransform->ulObjectID);
            if (lRet < 0)
            {
                DOS_ASSERT(0);

                blProcessOK = DOS_FALSE;
                break;
            }
        }
        else if (0 == dos_strnicmp(aszNames[lIndex], "object", dos_strlen("object")))
        {
            lRet = dos_atoul(aszValues[lIndex], &pstNumTransform->enObject);
            if (lRet < 0 || pstNumTransform->enObject >= SC_NUM_TRANSFORM_OBJECT_BUTT)
            {
                DOS_ASSERT(0);

                blProcessOK = DOS_FALSE;
                break;
            }
        }
        else if (0 == dos_strnicmp(aszNames[lIndex], "direction", dos_strlen("direction")))
        {
            lRet = dos_atoul(aszValues[lIndex], &pstNumTransform->enDirection);

            if (lRet < 0 || pstNumTransform->enDirection >= SC_NUM_TRANSFORM_DIRECTION_BUTT)
            {
                DOS_ASSERT(0);

                blProcessOK = DOS_FALSE;
                break;
            }
        }
        else if (0 == dos_strnicmp(aszNames[lIndex], "transform_timing", dos_strlen("transform_timing")))
        {
            lRet = dos_atoul(aszValues[lIndex], &pstNumTransform->enTiming);

            if (lRet < 0 || pstNumTransform->enTiming >= SC_NUM_TRANSFORM_TIMING_BUTT)
            {
                DOS_ASSERT(0);

                blProcessOK = DOS_FALSE;
                break;
            }
        }
        else if (0 == dos_strnicmp(aszNames[lIndex], "num_selection", dos_strlen("num_selection")))
        {
            lRet = dos_atoul(aszValues[lIndex], &pstNumTransform->enNumSelect);

            if (lRet < 0 || pstNumTransform->enNumSelect >= SC_NUM_TRANSFORM_SELECT_BUTT)
            {
                DOS_ASSERT(0);

                blProcessOK = DOS_FALSE;
                break;
            }
        }
        else if (0 == dos_strnicmp(aszNames[lIndex], "callee_prefixion", dos_strlen("callee_prefixion")))
        {
            if (aszValues[lIndex] && '\0' != aszValues[lIndex][0])
            {
                dos_strncpy(pstNumTransform->szCalleePrefix, aszValues[lIndex], sizeof(pstNumTransform->szCalleePrefix));
                pstNumTransform->szCalleePrefix[sizeof(pstNumTransform->szCalleePrefix) - 1] = '\0';
            }
            else
            {
                pstNumTransform->szCalleePrefix[0] = '\0';
            }
        }
        else if (0 == dos_strnicmp(aszNames[lIndex], "caller_prefixion", dos_strlen("caller_prefixion")))
        {
            if (aszValues[lIndex] && '\0' != aszValues[lIndex][0])
            {
                dos_strncpy(pstNumTransform->szCallerPrefix, aszValues[lIndex], sizeof(pstNumTransform->szCallerPrefix));
                pstNumTransform->szCallerPrefix[sizeof(pstNumTransform->szCallerPrefix) - 1] = '\0';
            }
            else
            {
                pstNumTransform->szCallerPrefix[0] = '\0';
            }
        }
        else if (0 == dos_strnicmp(aszNames[lIndex], "replace_all", dos_strlen("replace_all")))
        {
            if (DOS_ADDR_INVALID(aszValues[lIndex]))
            {
                DOS_ASSERT(0);

                blProcessOK = DOS_FALSE;
                break;
            }

            if (aszValues[lIndex][0] == '0')
            {
                pstNumTransform->bReplaceAll = DOS_FALSE;
            }
            else
            {
                pstNumTransform->bReplaceAll = DOS_TRUE;
            }
        }
        else if (0 == dos_strnicmp(aszNames[lIndex], "replace_num", dos_strlen("replace_num")))
        {
            if (aszValues[lIndex] && '\0' != aszValues[lIndex][0])
            {
                dos_strncpy(pstNumTransform->szReplaceNum, aszValues[lIndex], sizeof(pstNumTransform->szReplaceNum));
                pstNumTransform->szReplaceNum[sizeof(pstNumTransform->szReplaceNum) - 1] = '\0';
            }
            else
            {
                pstNumTransform->szReplaceNum[0] = '\0';
            }
        }
        else if (0 == dos_strnicmp(aszNames[lIndex], "del_left", dos_strlen("del_left")))
        {
            lRet = dos_atoul(aszValues[lIndex], &pstNumTransform->ulDelLeft);
            if (lRet < 0)
            {
                DOS_ASSERT(0);

                blProcessOK = DOS_FALSE;
                break;
            }
        }
        else if (0 == dos_strnicmp(aszNames[lIndex], "del_right", dos_strlen("del_right")))
        {
            lRet = dos_atoul(aszValues[lIndex], &pstNumTransform->ulDelRight);
            if (lRet < 0)
            {
                DOS_ASSERT(0);

                blProcessOK = DOS_FALSE;
                break;
            }
        }
        else if (0 == dos_strnicmp(aszNames[lIndex], "add_prefixion", dos_strlen("add_prefixion")))
        {
            if (aszValues[lIndex] && '\0' != aszValues[lIndex][0])
            {
                dos_strncpy(pstNumTransform->szAddPrefix, aszValues[lIndex], sizeof(pstNumTransform->szAddPrefix));
                pstNumTransform->szAddPrefix[sizeof(pstNumTransform->szAddPrefix) - 1] = '\0';
            }
            else
            {
                pstNumTransform->szAddPrefix[0] = '\0';
            }
        }
        else if (0 == dos_strnicmp(aszNames[lIndex], "add_suffix", dos_strlen("add_suffix")))
        {
            if (aszValues[lIndex] && '\0' != aszValues[lIndex][0])
            {
                dos_strncpy(pstNumTransform->szAddSuffix, aszValues[lIndex], sizeof(pstNumTransform->szAddSuffix));
                pstNumTransform->szAddSuffix[sizeof(pstNumTransform->szAddSuffix) - 1] = '\0';
            }
            else
            {
                pstNumTransform->szAddSuffix[0] = '\0';
            }
        }
        else if (0 == dos_strnicmp(aszNames[lIndex], "priority", dos_strlen("priority")))
        {
            lRet = dos_atoul(aszValues[lIndex], &pstNumTransform->enPriority);
            if (lRet < 0 || pstNumTransform->enPriority >= SC_NUM_TRANSFORM_PRIORITY_BUTT)
            {
                DOS_ASSERT(0);

                blProcessOK = DOS_FALSE;
                break;
            }
        }
        else if (0 == dos_strnicmp(aszNames[lIndex], "expiry", dos_strlen("expiry")))
        {
            lRet = dos_atoul(aszValues[lIndex], &pstNumTransform->ulExpiry);
            if (lRet < 0)
            {
                DOS_ASSERT(0);

                blProcessOK = DOS_FALSE;
                break;
            }

            if (0 == pstNumTransform->ulExpiry)
            {
                /* 无限制 */
                pstNumTransform->ulExpiry = U32_BUTT;
            }
        }
        else
        {
            DOS_ASSERT(0);

            blProcessOK = DOS_FALSE;
            break;
        }
    }

    if (!blProcessOK)
    {
        DOS_ASSERT(0);
        dos_dmem_free(pstNumTransform);
        pstNumTransform = NULL;

        return DOS_FAIL;
    }

    pthread_mutex_lock(&g_mutexNumTransformList);
    pstListNode = dll_find(&g_stNumTransformList, &pstNumTransform->ulID, sc_ep_num_transform_find);
    if (DOS_ADDR_INVALID(pstListNode))
    {
        pstListNode = (DLL_NODE_S *)dos_dmem_alloc(sizeof(DLL_NODE_S));
        if (DOS_ADDR_INVALID(pstListNode))
        {
            DOS_ASSERT(0);

            dos_dmem_free(pstNumTransform);
            pstNumTransform = NULL;

            pthread_mutex_unlock(&g_mutexNumTransformList);

            return DOS_FAIL;
        }

        DLL_Init_Node(pstListNode);
        pstListNode->pHandle = pstNumTransform;
        pstNumTransform->bExist = DOS_TRUE;
        DLL_Add(&g_stNumTransformList, pstListNode);
    }
    else
    {
        pstNumTransformTmp = pstListNode->pHandle;
        if (DOS_ADDR_INVALID(pstNumTransformTmp))
        {
            DOS_ASSERT(0);

            dos_dmem_free(pstNumTransform);
            pstNumTransform = NULL;

            pthread_mutex_unlock(&g_mutexNumTransformList);
            return DOS_FAIL;
        }

        dos_memcpy(pstNumTransformTmp, pstNumTransform, sizeof(SC_NUM_TRANSFORM_NODE_ST));
        pstNumTransform->bExist = DOS_TRUE;

        dos_dmem_free(pstNumTransform);
        pstNumTransform = NULL;
    }
    pthread_mutex_unlock(&g_mutexNumTransformList);

    return DOS_TRUE;
}


U32 sc_load_num_transform(U32 ulIndex)
{
    S8 szSQL[1024];
    S32 lRet;

    if (SC_INVALID_INDEX == ulIndex)
    {
        dos_snprintf(szSQL, sizeof(szSQL)
                    , "SELECT id, object, object_id, direction, transform_timing, num_selection, caller_prefixion, callee_prefixion, replace_all, replace_num, del_left, del_right, add_prefixion, add_suffix, priority, expiry FROM tbl_num_transformation  ORDER BY tbl_num_transformation.priority ASC;");
    }
    else
    {
        dos_snprintf(szSQL, sizeof(szSQL)
                    , "SELECT id, object, object_id, direction, transform_timing, num_selection, caller_prefixion, callee_prefixion, replace_all, replace_num, del_left, del_right, add_prefixion, add_suffix, priority, expiry FROM tbl_num_transformation where tbl_num_transformation.id=%u;"
                    , ulIndex);
    }

    lRet = db_query(g_pstSCDBHandle, szSQL, sc_load_num_transform_cb, NULL, NULL);
    if (DB_ERR_SUCC != lRet)
    {
        DOS_ASSERT(0);
        sc_logr_error(SC_FUNC, "Load Num Transform FAIL.(ID:%u)", ulIndex);
        return DOS_FAIL;
    }
    sc_logr_debug(SC_FUNC, "Load Num Transform SUCC.(ID:%u)", ulIndex);

    return DOS_SUCC;
}

S32 sc_load_customer_cb(VOID *pArg, S32 lCount, S8 **aszValues, S8 **aszNames)
{
    SC_CUSTOMER_NODE_ST  *pstCustomer       = NULL;
    SC_CUSTOMER_NODE_ST  *pstCustomerTmp    = NULL;
    DLL_NODE_S           *pstListNode       = NULL;
    S32                  lIndex;
    BOOL                 blProcessOK = DOS_FALSE;
    U32                  ulCallOutGroup;

    if (DOS_ADDR_INVALID(aszValues)
        || DOS_ADDR_INVALID(aszNames))
    {
        DOS_ASSERT(0);

        return DOS_FAIL;
    }

    pstCustomer = dos_dmem_alloc(sizeof(SC_CUSTOMER_NODE_ST));
    if (DOS_ADDR_INVALID(pstCustomer))
    {
        DOS_ASSERT(0);

        return DOS_FAIL;
    }

    sc_ep_customer_init(pstCustomer);

    for (blProcessOK = DOS_TRUE, lIndex = 0; lIndex < lCount; lIndex++)
    {
        if (0 == dos_strnicmp(aszNames[lIndex], "id", dos_strlen("id")))
        {
            if (dos_atoul(aszValues[lIndex], &pstCustomer->ulID) < 0)
            {
                DOS_ASSERT(0);

                blProcessOK = DOS_FALSE;
                break;
            }
        }
        else if (0 == dos_strnicmp(aszNames[lIndex], "call_out_group", dos_strlen("call_out_group")))
        {
            if (DOS_ADDR_INVALID(aszValues[lIndex])
                || '\0' == aszValues[lIndex][0]
                || dos_atoul(aszValues[lIndex], &ulCallOutGroup) < 0
                || ulCallOutGroup > U16_BUTT)
            {
                DOS_ASSERT(0);

                blProcessOK = DOS_FALSE;
                break;
            }

            pstCustomer->usCallOutGroup = (U16)ulCallOutGroup;
        }
    }

    if (!blProcessOK)
    {
        DOS_ASSERT(0);
        dos_dmem_free(pstCustomer);
        pstCustomer = NULL;
        return DOS_FAIL;
    }

    pthread_mutex_lock(&g_mutexCustomerList);
    pstListNode = dll_find(&g_stCustomerList, &pstCustomer->ulID, sc_ep_customer_find);
    if (DOS_ADDR_INVALID(pstListNode))
    {
        pstListNode = (DLL_NODE_S *)dos_dmem_alloc(sizeof(DLL_NODE_S));
        if (DOS_ADDR_INVALID(pstListNode))
        {
            DOS_ASSERT(0);

            dos_dmem_free(pstCustomer);
            pstCustomer = NULL;

            pthread_mutex_unlock(&g_mutexCustomerList);
            return DOS_FAIL;
        }

        DLL_Init_Node(pstListNode);
        pstListNode->pHandle = pstCustomer;
        pstCustomer->bExist = DOS_TRUE;
        DLL_Add(&g_stCustomerList, pstListNode);
    }
    else
    {
        pstCustomerTmp = pstListNode->pHandle;
        if (DOS_ADDR_INVALID(pstCustomerTmp))
        {
            DOS_ASSERT(0);

            dos_dmem_free(pstCustomer);
            pstCustomer = NULL;

            pthread_mutex_unlock(&g_mutexCustomerList);
            return DOS_FAIL;
        }

        dos_memcpy(pstCustomerTmp, pstCustomer, sizeof(SC_CUSTOMER_NODE_ST));
        pstCustomerTmp->bExist = DOS_TRUE;

        dos_dmem_free(pstCustomer);
        pstCustomer = NULL;
    }
    pthread_mutex_unlock(&g_mutexCustomerList);

    return DOS_TRUE;
}

U32 sc_load_customer(U32 ulIndex)
{
    S8 szSQL[1024];

    if (SC_INVALID_INDEX == ulIndex)
    {
        dos_snprintf(szSQL, sizeof(szSQL)
                    , "SELECT id, call_out_group FROM tbl_customer where tbl_customer.type=0;");
    }
    else
    {
        dos_snprintf(szSQL, sizeof(szSQL)
                    , "SELECT id, call_out_group FROM tbl_customer where tbl_customer.type=0 and tbl_customer.id=%u;"
                    , ulIndex);
    }

    db_query(g_pstSCDBHandle, szSQL, sc_load_customer_cb, NULL, NULL);

    return DOS_SUCC;
}

static S32 sc_load_number_lmt_cb(VOID *pArg, S32 lCount, S8 **aszValues, S8 **aszNames)
{
    HASH_NODE_S           *pstHashNode      = NULL;
    SC_NUMBER_LMT_NODE_ST *pstNumLmtNode    = NULL;
    S8                    *pszID            = NULL;
    S8                    *pszLimitationID  = NULL;
    S8                    *pszType          = NULL;
    S8                    *pszBID           = NULL;
    S8                    *pszHandle        = NULL;
    S8                    *pszCycle         = NULL;
    S8                    *pszTimes         = NULL;
    S8                    *pszCID           = NULL;
    S8                    *pszDID           = NULL;
    S8                    *pszNumber        = NULL;
    U32                   ulID              = U32_BUTT;
    U32                   ulLimitationID    = U32_BUTT;
    U32                   ulType            = U32_BUTT;
    U32                   ulBID             = U32_BUTT;
    U32                   ulHandle          = U32_BUTT;
    U32                   ulCycle           = U32_BUTT;
    U32                   ulTimes           = U32_BUTT;
    U32                   ulHashIndex       = U32_BUTT;

    if (lCount < 9)
    {
        DOS_ASSERT(0);
        return DOS_FAIL;
    }
    pszID            = aszValues[0];
    pszLimitationID  = aszValues[1];
    pszType          = aszValues[2];
    pszBID           = aszValues[3];
    pszHandle        = aszValues[4];
    pszCycle         = aszValues[5];
    pszTimes         = aszValues[6];
    pszCID           = aszValues[7];
    pszDID           = aszValues[8];

    if (DOS_ADDR_INVALID(pszID)
        || DOS_ADDR_INVALID(pszLimitationID)
        || DOS_ADDR_INVALID(pszType)
        || DOS_ADDR_INVALID(pszBID)
        || DOS_ADDR_INVALID(pszHandle)
        || DOS_ADDR_INVALID(pszCycle)
        || DOS_ADDR_INVALID(pszTimes)
        || dos_atoul(pszID, &ulID) < 0
        || dos_atoul(pszLimitationID, &ulLimitationID) < 0
        || dos_atoul(pszType, &ulType) < 0
        || dos_atoul(pszBID, &ulBID) < 0
        || dos_atoul(pszHandle, &ulHandle) < 0
        || dos_atoul(pszCycle, &ulCycle) < 0
        || dos_atoul(pszTimes, &ulTimes) < 0)
    {
        DOS_ASSERT(0);
        return DOS_FAIL;
    }

    switch (ulType)
    {
        case SC_NUM_LMT_TYPE_DID:
            pszNumber = pszDID;
            break;
        case SC_NUM_LMT_TYPE_CALLER:
            pszNumber = pszCID;
            break;
        default:
            DOS_ASSERT(0);
            return DOS_FAIL;
    }

    if (DOS_ADDR_INVALID(pszNumber)
        || '\0' == pszNumber[0])
    {
        DOS_ASSERT(0);
        return DOS_FAIL;
    }

    if (ulHandle >= SC_NUM_LMT_HANDLE_BUTT)
    {
        DOS_ASSERT(0);
        return DOS_FAIL;
    }

    ulHashIndex = sc_ep_number_lmt_hash_func(pszNumber);
    pthread_mutex_lock(&g_mutexHashNumberlmt);
    pstHashNode = hash_find_node(g_pstHashNumberlmt, ulHashIndex, pszNumber, sc_ep_number_lmt_find);
    if (DOS_ADDR_VALID(pstHashNode))
    {
        pstNumLmtNode= pstHashNode->pHandle;
        if (DOS_ADDR_VALID(pstNumLmtNode))
        {
            pstNumLmtNode->ulID      = ulID;
            pstNumLmtNode->ulGrpID   = ulLimitationID;
            pstNumLmtNode->ulHandle  = ulHandle;
            pstNumLmtNode->ulLimit   = ulTimes;
            pstNumLmtNode->ulCycle   = ulCycle;
            pstNumLmtNode->ulType    = ulType;
            pstNumLmtNode->ulNumberID= ulBID;
            dos_strncpy(pstNumLmtNode->szPrefix, pszNumber, sizeof(pstNumLmtNode->szPrefix));
            pstNumLmtNode->szPrefix[sizeof(pstNumLmtNode->szPrefix) - 1] = '\0';
        }
        else
        {
            pstNumLmtNode = dos_dmem_alloc(sizeof(SC_NUMBER_LMT_NODE_ST));
            if (DOS_ADDR_INVALID(pstNumLmtNode))
            {
                DOS_ASSERT(0);
                pthread_mutex_unlock(&g_mutexHashNumberlmt);

                return DOS_FAIL;
            }

            pstNumLmtNode->ulID      = ulID;
            pstNumLmtNode->ulGrpID   = ulLimitationID;
            pstNumLmtNode->ulHandle  = ulHandle;
            pstNumLmtNode->ulLimit   = ulTimes;
            pstNumLmtNode->ulCycle   = ulCycle;
            pstNumLmtNode->ulType    = ulType;
            pstNumLmtNode->ulNumberID= ulBID;
            dos_strncpy(pstNumLmtNode->szPrefix, pszNumber, sizeof(pstNumLmtNode->szPrefix));
            pstNumLmtNode->szPrefix[sizeof(pstNumLmtNode->szPrefix) - 1] = '\0';

            pstHashNode->pHandle = pstNumLmtNode;
        }
    }
    else
    {
        pstHashNode = dos_dmem_alloc(sizeof(HASH_NODE_S));
        pstNumLmtNode = dos_dmem_alloc(sizeof(SC_NUMBER_LMT_NODE_ST));
        if (DOS_ADDR_INVALID(pstHashNode)
            || DOS_ADDR_INVALID(pstNumLmtNode))
        {
            DOS_ASSERT(0);
            pthread_mutex_unlock(&g_mutexHashNumberlmt);

            if (DOS_ADDR_VALID(pstHashNode))
            {
                dos_dmem_free(pstHashNode);
                pstHashNode = NULL;
            }

            if (DOS_ADDR_VALID(pstNumLmtNode))
            {
                dos_dmem_free(pstNumLmtNode);
                pstNumLmtNode = NULL;
            }

            return DOS_FAIL;
        }

        pstNumLmtNode->ulID      = ulID;
        pstNumLmtNode->ulGrpID   = ulLimitationID;
        pstNumLmtNode->ulHandle  = ulHandle;
        pstNumLmtNode->ulLimit   = ulTimes;
        pstNumLmtNode->ulCycle   = ulCycle;
        pstNumLmtNode->ulType    = ulType;
        pstNumLmtNode->ulNumberID= ulBID;
        dos_strncpy(pstNumLmtNode->szPrefix, pszNumber, sizeof(pstNumLmtNode->szPrefix));
        pstNumLmtNode->szPrefix[sizeof(pstNumLmtNode->szPrefix) - 1] = '\0';

        HASH_Init_Node(pstHashNode);
        pstHashNode->pHandle = pstNumLmtNode;

        hash_add_node(g_pstHashNumberlmt, pstHashNode, ulHashIndex, NULL);
    }
    pthread_mutex_unlock(&g_mutexHashNumberlmt);

    return DOS_SUCC;
}

U32 sc_load_number_lmt(U32 ulIndex)
{
    S8 szSQL[1024];

    if (SC_INVALID_INDEX == ulIndex)
    {
        dos_snprintf(szSQL, sizeof(szSQL)
                    , "SELECT a.id, a.limitation_id, a.btype, a.bid, b.handle, " \
                      "b.cycle, b.times, c.cid, d.did_number " \
                      "FROM tbl_caller_limitation_assign a " \
                      "LEFT JOIN tbl_caller_limitation b ON a.limitation_id = b.id " \
                      "LEFT JOIN tbl_caller c ON a.btype = %u AND a.bid = c.id " \
                      "LEFT JOIN tbl_sipassign d ON a.btype = %u AND a.bid = d.id "
                    , SC_NUM_LMT_TYPE_CALLER, SC_NUM_LMT_TYPE_DID);
    }
    else
    {
        dos_snprintf(szSQL, sizeof(szSQL)
                    , "SELECT a.id, a.limitation_id, a.btype, a.bid, b.handle, " \
                      "b.cycle, b.times, c.cid, d.did_number " \
                      "FROM tbl_caller_limitation_assign a " \
                      "LEFT JOIN tbl_caller_limitation b ON a.limitation_id = b.id " \
                      "LEFT JOIN tbl_caller c ON a.btype = %u AND a.bid = c.id " \
                      "LEFT JOIN tbl_sipassign d ON a.btype = %u AND a.bid = d.id " \
                      "WHERE a.id = %u"
                    , SC_NUM_LMT_TYPE_CALLER, SC_NUM_LMT_TYPE_DID, ulIndex);

    }

    if (db_query(g_pstSCDBHandle, szSQL, sc_load_number_lmt_cb, NULL, NULL) != DB_ERR_SUCC)
    {
        DOS_ASSERT(0);

        sc_logr_error(SC_ESL, "%s", "Load number limitation fail.");
        return DOS_FAIL;
    }


    sc_num_lmt_update(0, NULL);

    return DOS_SUCC;
}

U32 sc_del_number_lmt(U32 ulIndex)
{
    U32                   ulHashIndex       = U32_BUTT;
    HASH_NODE_S           *pstHashNode      = NULL;
    SC_NUMBER_LMT_NODE_ST *pstNumLmtNode    = NULL;

    pthread_mutex_lock(&g_mutexHashNumberlmt);
    HASH_Scan_Table(g_pstHashNumberlmt, ulHashIndex)
    {
        HASH_Scan_Bucket(g_pstHashNumberlmt, ulHashIndex, pstHashNode, HASH_NODE_S*)
        {
            if (DOS_ADDR_INVALID(pstHashNode)
                || DOS_ADDR_INVALID(pstHashNode->pHandle))
            {
                continue;
            }

            pstNumLmtNode = pstHashNode->pHandle;
            if (pstNumLmtNode->ulID == ulIndex)
            {
                hash_delete_node(g_pstHashNumberlmt, pstHashNode, ulHashIndex);
                pthread_mutex_unlock(&g_mutexHashNumberlmt);

                return DOS_SUCC;
            }
        }
    }
    pthread_mutex_unlock(&g_mutexHashNumberlmt);

    return DOS_FAIL;
}


/**
  * 函数名: U32 sc_del_invalid_gateway()
  * 参数:
  * 功能: 删除掉内存中残留但数据库没有的数据
  * 返回: 成功返回DOS_SUCC，失败返回DOS_FAIL
  **/
U32 sc_del_invalid_gateway()
{
    HASH_NODE_S   *pstHashNode = NULL;
    SC_GW_NODE_ST *pstGWNode   = NULL;
    U32   ulHashIndex = U32_BUTT;
    U32   ulGWID = U32_BUTT, ulRet = U32_BUTT;
    S8    szBuff[64] = {0};

    HASH_Scan_Table(g_pstHashGW,ulHashIndex)
    {
        HASH_Scan_Bucket(g_pstHashGW, ulHashIndex, pstHashNode, HASH_NODE_S *)
        {
            if (DOS_ADDR_INVALID(pstHashNode)
                || DOS_ADDR_INVALID(pstHashNode->pHandle))
            {
                continue;
            }
            pstGWNode = (SC_GW_NODE_ST *)pstHashNode->pHandle;
            /* 如果说内存里有该条记录，但数据库没有该数据，删之 */
            if (DOS_FALSE == pstGWNode->bExist)
            {
                /* 记录网关id */
                ulGWID = pstGWNode->ulGWID;
                /* FreeSWITCH删除该配置数据 */
                ulRet = py_exec_func("router", "del_route", "(k)", (U64)ulGWID);
                if (DOS_SUCC != ulRet)
                {
                    DOS_ASSERT(0);
                    return DOS_FAIL;
                }
                /* FreeSWITCH删除内存中该数据 */
                dos_snprintf(szBuff, sizeof(szBuff), "bgapi sofia profile external killgw %u", ulGWID);
                ulRet = sc_ep_esl_execute_cmd(szBuff);
                if (DOS_SUCC != ulRet)
                {
                    DOS_ASSERT(0);
                }

                /* 从节点中删除数据 */
                hash_delete_node(g_pstHashGW, pstHashNode, ulHashIndex);
                if (DOS_ADDR_VALID(pstGWNode))
                {
                    dos_dmem_free(pstGWNode);
                    pstGWNode = NULL;
                }
                if (DOS_ADDR_VALID(pstHashNode))
                {
                    dos_dmem_free(pstHashNode);
                    pstHashNode = NULL;
                }
            }
        }
    }
    return DOS_SUCC;
}

/**
  * 函数名: U32 sc_del_invalid_route()
  * 参数:
  * 功能: 删除掉内存中残留但数据库没有的数据
  * 返回: 成功返回DOS_SUCC，失败返回DOS_FAIL
  **/
U32 sc_del_invalid_route()
{
    SC_ROUTE_NODE_ST  *pstRoute = NULL;
    DLL_NODE_S        *pstNode  = NULL;

    DLL_Scan(&g_stRouteList, pstNode, DLL_NODE_S *)
    {
        if (DOS_ADDR_INVALID(pstNode)
            || DOS_ADDR_INVALID(pstNode->pHandle))
        {
            continue;
        }

        pstRoute = (SC_ROUTE_NODE_ST *)pstNode->pHandle;
        /* 如果说该数据并非来自数据库，删之 */
        if (DOS_FALSE == pstRoute->bExist)
        {
            dll_delete(&g_stRouteList, pstNode);
            if (DOS_ADDR_VALID(pstRoute))
            {
                dos_dmem_free(pstRoute);
                pstRoute = NULL;
            }
            if (DOS_ADDR_VALID(pstNode))
            {
                dos_dmem_free(pstNode);
                pstNode = NULL;
            }
        }
        else
        {
            /* 并将所有的节点该标志置为false */
            pstRoute->bExist = DOS_FALSE;
        }
    }

    return DOS_SUCC;
}

U32 sc_del_invalid_gateway_grp()
{
    SC_GW_GRP_NODE_ST * pstGWGrp = NULL;
    HASH_NODE_S *pstHashNode = NULL;
    U32  ulHashIndex = 0;

    HASH_Scan_Table(g_pstHashGWGrp, ulHashIndex)
    {
        HASH_Scan_Bucket(g_pstHashGWGrp, ulHashIndex, pstHashNode, HASH_NODE_S *)
        {
            if (DOS_ADDR_INVALID(pstHashNode)
                || DOS_ADDR_INVALID(pstHashNode->pHandle))
            {
                continue;
            }

            pstGWGrp = (SC_GW_GRP_NODE_ST *)pstHashNode->pHandle;
            if (DOS_FALSE == pstGWGrp->bExist)
            {
                /* 删除之 */
                sc_gateway_grp_delete(pstGWGrp->bExist);
            }
            else
            {
                pstGWGrp->bExist = DOS_FALSE;
            }
        }
    }
    return DOS_SUCC;
}

U32 sc_del_tt_number(U32 ulIndex)
{
    HASH_NODE_S    *pstHashNode;
    U32            ulHashIndex;

    pthread_mutex_lock(&g_mutexHashTTNumber);
    ulHashIndex = sc_ep_tt_hash_func(ulIndex);
    pstHashNode = hash_find_node(g_pstHashTTNumber, ulHashIndex, &ulIndex, sc_ep_tt_list_find);
    if (DOS_ADDR_VALID(pstHashNode))
    {
        hash_delete_node(g_pstHashTTNumber, pstHashNode, ulHashIndex);

        pthread_mutex_unlock(&g_mutexHashTTNumber);
        return DOS_SUCC;
    }
    else
    {
        pthread_mutex_unlock(&g_mutexHashTTNumber);

        return DOS_FAIL;
    }
}

U32 sc_save_number_stat(U32 ulType, S8 *pszNumber, U32 ulTimes)
{
    S8        szSQL[512];

    if (ulType >= SC_NUM_LMT_TYPE_BUTT
        || DOS_ADDR_INVALID(pszNumber))
    {
        DOS_ASSERT(0);
        return DOS_FAIL;
    }

    dos_snprintf(szSQL, sizeof(szSQL)
                    , "INSERT INTO tbl_stat_caller(type, ctime, caller, times) VALUE(%u, %u, \"%s\", %u)"
                    , ulType, time(NULL), pszNumber, ulTimes);

    if (db_query(g_pstSCDBHandle, szSQL, NULL, NULL, NULL) != DOS_SUCC)
    {
        DOS_ASSERT(0);
        return DOS_FAIL;
    }

    return DOS_SUCC;
}

S32 sc_update_number_stat_cb(VOID *pArg, S32 lCount, S8 **aszValues, S8 **aszNames)
{
    U32 ulTimes;

    if (lCount != 0
        || DOS_ADDR_INVALID(pArg)
        || DOS_ADDR_INVALID(aszValues)
        || DOS_ADDR_INVALID(aszNames))
    {
        DOS_ASSERT(0);
        return DOS_FAIL;
    }

    if (DOS_ADDR_INVALID(aszValues[0])
        || DOS_ADDR_INVALID(aszNames[0]))
    {
        DOS_ASSERT(0);
        return DOS_FAIL;
    }

    if ('\0' == aszValues[0][0]
        || '\0' == aszNames[0][0]
        || dos_strcmp(aszNames[0], "times")
        || dos_atoul(aszValues[0], &ulTimes) < 0)
    {
        DOS_ASSERT(0);
        return DOS_FAIL;
    }

    *(U32 *)pArg = ulTimes;

    return DOS_SUCC;
}

U32 sc_update_number_stat(U32 ulType, U32 ulCycle, S8 *pszNumber)
{
    U32 ulStartTimestamp;
    U32 ulTimes;
    time_t    ulTime;
    struct tm *pstTime;
    struct tm stStartTime;
    S8        szSQL[512];

    if (ulType >= SC_NUM_LMT_TYPE_BUTT
        || ulCycle >= SC_NUMBER_LMT_CYCLE_BUTT
        || DOS_ADDR_INVALID(pszNumber))
    {
        DOS_ASSERT(0);
        return DOS_FAIL;
    }

    ulTime = time(NULL);
    pstTime = localtime(&ulTime);
    stStartTime.tm_sec   = 59;
    stStartTime.tm_min   = 59;
    stStartTime.tm_hour  = 23;
    stStartTime.tm_wday  = 0;
    stStartTime.tm_yday  = 0;
    stStartTime.tm_isdst = 0;

    switch (ulCycle)
    {
        case SC_NUMBER_LMT_CYCLE_DAY:
            stStartTime.tm_mday  = pstTime->tm_mday - 1;
            stStartTime.tm_mon   = pstTime->tm_mon;
            stStartTime.tm_year  = pstTime->tm_year;
            break;

        case SC_NUMBER_LMT_CYCLE_WEEK:
            stStartTime.tm_mday  = pstTime->tm_mday - pstTime->tm_wday;
            stStartTime.tm_mon   = pstTime->tm_mon;
            stStartTime.tm_year  = pstTime->tm_year;
            break;

        case SC_NUMBER_LMT_CYCLE_MONTH:
            stStartTime.tm_mday  = 0;
            stStartTime.tm_mon   = pstTime->tm_mon;
            stStartTime.tm_year  = pstTime->tm_year;
            break;

        case SC_NUMBER_LMT_CYCLE_YEAR:
            stStartTime.tm_mday  = 0;
            stStartTime.tm_mon   = 0;
            stStartTime.tm_year  = pstTime->tm_year;
            break;

        default:
            DOS_ASSERT(0);
            return 0;
    }

    ulStartTimestamp = mktime(&stStartTime);

    dos_snprintf(szSQL, sizeof(szSQL)
        , "SELECT SUM(times) AS times FROM tbl_stat_caller WHERE ctime > %u AND type=%u AND caller=%s"
        , ulStartTimestamp, ulType, pszNumber);

    if (db_query(g_pstSCDBHandle, szSQL, sc_update_number_stat_cb, &ulTimes, NULL) != DOS_SUCC)
    {
        DOS_ASSERT(0);
        return 0;
    }

    return ulTimes;
}

U32 sc_num_lmt_stat(U32 ulType, VOID *ptr)
{
    U32 ulHashIndex;
    U32 ulHashIndexMunLmt;
    U32 ulTimes = 0;
    HASH_NODE_S             *pstHashNodeNumber = NULL;
    HASH_NODE_S             *pstHashNodeNumLmt = NULL;
    SC_CALLER_QUERY_NODE_ST *ptCaller          = NULL;
    SC_DID_NODE_ST          *ptDIDNumber       = NULL;
    SC_NUMBER_LMT_NODE_ST   *pstNumLmt         = NULL;

    pthread_mutex_lock(&g_mutexHashCaller);
    HASH_Scan_Table(g_pstHashCaller, ulHashIndex)
    {
        HASH_Scan_Bucket(g_pstHashCaller, ulHashIndex, pstHashNodeNumber, HASH_NODE_S*)
        {
            if (DOS_ADDR_INVALID(pstHashNodeNumber)
                || DOS_ADDR_INVALID(pstHashNodeNumber->pHandle))
            {
                continue;
            }

            ptCaller = pstHashNodeNumber->pHandle;
            ulTimes = ptCaller->ulTimes;
            ptCaller->ulTimes = 0;

            sc_save_number_stat(SC_NUM_LMT_TYPE_CALLER, ptCaller->szNumber, ulTimes);

            ulHashIndexMunLmt = sc_ep_number_lmt_hash_func(ptCaller->szNumber);
            pthread_mutex_lock(&g_mutexHashNumberlmt);
            pstHashNodeNumLmt= hash_find_node(g_pstHashNumberlmt, ulHashIndexMunLmt, ptCaller->szNumber, sc_ep_number_lmt_find);
            if (DOS_ADDR_INVALID(pstHashNodeNumLmt)
                || DOS_ADDR_INVALID(pstHashNodeNumLmt->pHandle))
            {
                pthread_mutex_unlock(&g_mutexHashNumberlmt);
                continue;
            }

            pstNumLmt = pstHashNodeNumLmt->pHandle;
            pstNumLmt->ulStatUsed += ulTimes;

            pthread_mutex_unlock(&g_mutexHashNumberlmt);
        }
    }
    pthread_mutex_unlock(&g_mutexHashCaller);

    pthread_mutex_lock(&g_mutexHashDIDNum);
    HASH_Scan_Table(g_pstHashDIDNum, ulHashIndex)
    {
        HASH_Scan_Bucket(g_pstHashDIDNum, ulHashIndex, pstHashNodeNumber, HASH_NODE_S*)
        {
            if (DOS_ADDR_INVALID(pstHashNodeNumber)
                || DOS_ADDR_INVALID(pstHashNodeNumber->pHandle))
            {
                continue;
            }

            ptDIDNumber = pstHashNodeNumber->pHandle;
            if (DOS_FALSE == ptDIDNumber->bValid)
            {
                continue;
            }
            ulTimes = ptDIDNumber->ulTimes;
            ptDIDNumber->ulTimes = 0;

            sc_save_number_stat(SC_NUM_LMT_TYPE_DID, ptDIDNumber->szDIDNum, ulTimes);

            ulHashIndexMunLmt = sc_ep_number_lmt_hash_func(ptDIDNumber->szDIDNum);
            pthread_mutex_lock(&g_mutexHashNumberlmt);
            pstHashNodeNumLmt= hash_find_node(g_pstHashNumberlmt, ulHashIndexMunLmt, ptDIDNumber->szDIDNum, sc_ep_number_lmt_find);
            if (DOS_ADDR_INVALID(pstHashNodeNumLmt)
                || DOS_ADDR_INVALID(pstHashNodeNumLmt->pHandle))
            {
                pthread_mutex_unlock(&g_mutexHashNumberlmt);
                continue;
            }

            pstNumLmt = pstHashNodeNumLmt->pHandle;
            pstNumLmt->ulStatUsed += ulTimes;
            pthread_mutex_unlock(&g_mutexHashNumberlmt);
        }
    }
    pthread_mutex_unlock(&g_mutexHashDIDNum);

    return DOS_SUCC;
}

U32 sc_num_lmt_update(U32 ulType, VOID *ptr)
{
    U32 ulHashIndex;
    U32 ulTimes = 0;
    HASH_NODE_S             *pstHashNode       = NULL;
    SC_NUMBER_LMT_NODE_ST   *pstNumLmt         = NULL;

    pthread_mutex_lock(&g_mutexHashNumberlmt);

    HASH_Scan_Table(g_pstHashNumberlmt, ulHashIndex)
    {
        HASH_Scan_Bucket(g_pstHashNumberlmt, ulHashIndex, pstHashNode, HASH_NODE_S*)
        {
            if (DOS_ADDR_INVALID(pstHashNode)
                || DOS_ADDR_INVALID(pstHashNode->pHandle))
            {
                continue;
            }

            pstNumLmt = pstHashNode->pHandle;

            ulTimes = sc_update_number_stat(SC_NUM_LMT_TYPE_CALLER, pstNumLmt->ulCycle, pstNumLmt->szPrefix);
            pstNumLmt->ulStatUsed = ulTimes;
        }
    }

    pthread_mutex_unlock(&g_mutexHashNumberlmt);

    return DOS_SUCC;
}

BOOL sc_num_lmt_check(U32 ulType, U32 ulCurrentTime, S8 *pszNumber)
{
    U32  ulHashIndexMunLmt  = 0;
    U32  ulHandleType       = 0;
    BOOL blReachLmt         = DOS_FALSE;
    HASH_NODE_S             *pstHashNodeNumLmt = NULL;
    SC_NUMBER_LMT_NODE_ST   *pstNumLmt         = NULL;

    if (DOS_ADDR_INVALID(pszNumber)
        || '\0' == pszNumber[0]
        || ulType >= SC_NUM_LMT_TYPE_BUTT)
    {
        DOS_ASSERT(0);

        return DOS_TRUE;
    }

    ulHashIndexMunLmt = sc_ep_number_lmt_hash_func(pszNumber);
    pthread_mutex_lock(&g_mutexHashNumberlmt);
    pstHashNodeNumLmt= hash_find_node(g_pstHashNumberlmt, ulHashIndexMunLmt, pszNumber, sc_ep_number_lmt_find);
    if (DOS_ADDR_INVALID(pstHashNodeNumLmt)
        || DOS_ADDR_INVALID(pstHashNodeNumLmt->pHandle))
    {
        pthread_mutex_unlock(&g_mutexHashNumberlmt);
        DOS_ASSERT(0);

        sc_debug(SC_ESL, LOG_LEVEL_DEBUG
                , "Number limit check for \"%s\", There is no limitation for this number."
                , pszNumber);
        return DOS_TRUE;
    }

    if (pstNumLmt->ulStatUsed + ulCurrentTime >= pstNumLmt->ulLimit)
    {
        DOS_ASSERT(0);
        blReachLmt = DOS_TRUE;
        sc_debug(SC_ESL, LOG_LEVEL_WARNING
                , "Number limit check for \"%s\", This number has reached the limitation. Cycle: %u, Limitation: %u, Used: %u"
                , pszNumber, pstNumLmt->ulCycle, pstNumLmt->ulLimit, pstNumLmt->ulStatUsed + ulCurrentTime);
    }

    ulHandleType = pstNumLmt->ulHandle;

    pthread_mutex_unlock(&g_mutexHashNumberlmt);

    sc_debug(SC_ESL, LOG_LEVEL_WARNING
            , "Number limit check for \"%s\", This number has reached the limitation. Process as handle: %u"
            , pszNumber, pstNumLmt->ulHandle);

    switch (ulHandleType)
    {
        case SC_NUM_LMT_HANDLE_REJECT:
            return DOS_FALSE;
            break;

        default:
            DOS_ASSERT(0);
            return DOS_TRUE;
    }


    return DOS_TRUE;
}


U32 sc_ep_get_callout_group_by_customerID(U32 ulCustomerID, U16 *pusCallOutGroup)
{
    SC_CUSTOMER_NODE_ST  *pstCustomer       = NULL;
    DLL_NODE_S           *pstListNode       = NULL;

    if (DOS_ADDR_INVALID(pusCallOutGroup))
    {
        return DOS_FAIL;
    }

    pthread_mutex_lock(&g_mutexCustomerList);
    pstListNode = dll_find(&g_stCustomerList, &ulCustomerID, sc_ep_customer_find);
    if (DOS_ADDR_INVALID(pstListNode)
        || DOS_ADDR_INVALID(pstListNode->pHandle))
    {
        pstCustomer = (SC_CUSTOMER_NODE_ST *)pstListNode->pHandle;
        *pusCallOutGroup = pstCustomer->usCallOutGroup;
        pthread_mutex_unlock(&g_mutexCustomerList);

        return DOS_SUCC;
    }

    pthread_mutex_unlock(&g_mutexCustomerList);

    return DOS_FAIL;
}

U32 sc_ep_customer_list_find(U32 ulCustomerID)
{
    SC_CUSTOMER_NODE_ST  *pstCustomer       = NULL;
    DLL_NODE_S           *pstListNode       = NULL;

    pthread_mutex_lock(&g_mutexCustomerList);
    DLL_Scan(&g_stCustomerList, pstListNode, DLL_NODE_S *)
    {
        pstCustomer = (SC_CUSTOMER_NODE_ST *)pstListNode->pHandle;
        if (DOS_ADDR_INVALID(pstCustomer))
        {
            continue;
        }

        if (pstCustomer->ulID == ulCustomerID)
        {
            pthread_mutex_unlock(&g_mutexCustomerList);
            return DOS_SUCC;
        }
    }

    pthread_mutex_unlock(&g_mutexCustomerList);

    return DOS_FAIL;
}

/*
 * 函  数: U32 sc_ep_num_transform(SC_SCB_ST *pstSCB, U32 ulTrunkID, SC_NUM_TRANSFORM_TIMING_EN enTiming, SC_NUM_TRANSFORM_DIRECTION_EN enDirection, SC_NUM_TRANSFORM_SELECT_EN enNumSelect)
 * 功  能: 号码变换
 * 参  数:
 *      SC_SCB_ST *pstSCB                           : 呼叫控制块
 *      U32 ulTrunkID                               : 中继ID,(只有路由后才会匹配中继)
 *      SC_NUM_TRANSFORM_TIMING_EN      enTiming    : 路由前/路由后
 *      SC_NUM_TRANSFORM_DIRECTION_EN   enDirection : 呼入/呼出
 *      SC_NUM_TRANSFORM_SELECT_EN      enNumSelect : 主被叫选择
 * 返回值: 成功返回DOS_SUCC，否则返回DOS_FAIL
 **/
U32 sc_ep_num_transform(SC_SCB_ST *pstSCB, U32 ulTrunkID, SC_NUM_TRANSFORM_TIMING_EN enTiming, SC_NUM_TRANSFORM_SELECT_EN enNumSelect)
{
    SC_NUM_TRANSFORM_NODE_ST        *pstNumTransform        = NULL;
    SC_NUM_TRANSFORM_NODE_ST        *pstNumTransformEntry   = NULL;
    DLL_NODE_S                      *pstListNode            = NULL;
    SC_NUM_TRANSFORM_DIRECTION_EN   enDirection;
    S8  szNeedTransformNum[SC_TEL_NUMBER_LENGTH]        = {0};
    U32 ulIndex      = 0;
    S32 lIndex       = 0;
    U32 ulNumLen     = 0;
    U32 ulOffsetLen  = 0;
    time_t ulCurrTime   = time(NULL);

    if (DOS_ADDR_INVALID(pstSCB)
        || '\0' == pstSCB->szCalleeNum[0]
        || '\0' == pstSCB->szCallerNum[0]
        || enTiming >= SC_NUM_TRANSFORM_TIMING_BUTT
        || enNumSelect >= SC_NUM_TRANSFORM_SELECT_BUTT)
    {
        DOS_ASSERT(0);
        return DOS_FAIL;
    }

    sc_logr_debug(SC_DIALER, "Search number transfer rule for the task %d, timing is : %d, number select : %d"
                                , pstSCB->ulTaskID, enTiming, enNumSelect);

    /* 判断一下呼叫方向 呼入/呼出 */
    if (pstSCB->ucLegRole == SC_CALLEE && pstSCB->bIsAgentCall)
    {
        /* 被叫是坐席则为呼入, 否则为呼出 */
        enDirection = SC_NUM_TRANSFORM_DIRECTION_IN;
    }
    else
    {
        enDirection = SC_NUM_TRANSFORM_DIRECTION_OUT;
    }

    sc_logr_debug(SC_DIALER, "call firection : %d, pstSCB->ucLegRole : %d, pstSCB->bIsAgentCall : %d", enDirection, pstSCB->ucLegRole, pstSCB->bIsAgentCall);

    /* 遍历号码变换规则的链表，查找没有过期的，优先级别高的，针对这个客户或者系统的变换规则。
        先按优先级，同优先级，客户优先于中继，中继优先于系统 */
    pthread_mutex_lock(&g_mutexNumTransformList);
    DLL_Scan(&g_stNumTransformList, pstListNode, DLL_NODE_S *)
    {
        pstNumTransformEntry = (SC_NUM_TRANSFORM_NODE_ST *)pstListNode->pHandle;
        if (DOS_ADDR_INVALID(pstNumTransformEntry))
        {
            continue;
        }

        /* 判断有效期 */
        if (pstNumTransformEntry->ulExpiry < ulCurrTime)
        {
            continue;
        }

        /* 判断 路由前/后 */
        if (pstNumTransformEntry->enTiming != enTiming)
        {
            continue;
        }
        /* 判断 呼叫方向 */
        if (pstNumTransformEntry->enDirection != enDirection)
        {
            continue;
        }
        /* 判断主被叫 */
        if (pstNumTransformEntry->enNumSelect != enNumSelect)
        {
            continue;
        }

        /* 判断主叫号码前缀 */
        if ('\0' != pstNumTransformEntry->szCallerPrefix[0])
        {
            if (0 != dos_strnicmp(pstNumTransformEntry->szCallerPrefix, pstSCB->szCallerNum, dos_strlen(pstNumTransformEntry->szCallerPrefix)))
            {
                continue;
            }
        }

        /* 判断被叫号码前缀 */
        if ('\0' != pstNumTransformEntry->szCalleePrefix[0])
        {
            if (0 != dos_strnicmp(pstNumTransformEntry->szCalleePrefix, pstSCB->szCalleeNum, dos_strlen(pstNumTransformEntry->szCalleePrefix)))
            {
                continue;
            }
        }

        sc_logr_debug(SC_DIALER, "Call Object : %d", pstNumTransformEntry->enObject);

        if (SC_NUM_TRANSFORM_OBJECT_CUSTOMER == pstNumTransformEntry->enObject)
        {
            /* 针对客户 */
            if (pstNumTransformEntry->ulObjectID == pstSCB->ulCustomID)
            {
                if (DOS_ADDR_INVALID(pstNumTransform))
                {
                    pstNumTransform = pstNumTransformEntry;
                    continue;
                }

                if (pstNumTransformEntry->enPriority < pstNumTransform->enPriority)
                {
                    /* 选择优先级高的 */
                    pstNumTransform = pstNumTransformEntry;

                    continue;
                }

                if (pstNumTransformEntry->enPriority == pstNumTransform->enPriority && pstNumTransform->enObject != SC_NUM_TRANSFORM_OBJECT_CUSTOMER)
                {
                    /* 优先级相同，选择客户的 */
                    pstNumTransform = pstNumTransformEntry;

                    continue;
                }
            }
        }
        else if (SC_NUM_TRANSFORM_OBJECT_SYSTEM == pstNumTransformEntry->enObject)
        {
            /* 针对系统 */
            if (DOS_ADDR_INVALID(pstNumTransform))
            {
                pstNumTransform = pstNumTransformEntry;

                continue;
            }

            if (pstNumTransformEntry->enPriority < pstNumTransform->enPriority)
            {
                /* 选择优先级高的 */
                pstNumTransform = pstNumTransformEntry;

                continue;
            }
        }
        else if (SC_NUM_TRANSFORM_OBJECT_TRUNK == pstNumTransformEntry->enObject)
        {
            /* 针对中继，只有路由后，才需要判断这种情况 */
            if (enTiming == SC_NUM_TRANSFORM_TIMING_AFTER)
            {
                if (DOS_ADDR_INVALID(pstNumTransform))
                {
                    pstNumTransform = pstNumTransformEntry;
                    continue;
                }

                if (pstNumTransformEntry->ulObjectID != ulTrunkID)
                {
                    continue;
                }

                if (pstNumTransformEntry->enPriority < pstNumTransform->enPriority)
                {
                    /* 选择优先级高的 */
                    pstNumTransform = pstNumTransformEntry;

                    continue;
                }

                if (pstNumTransformEntry->enPriority == pstNumTransform->enPriority && pstNumTransform->enObject == SC_NUM_TRANSFORM_OBJECT_SYSTEM)
                {
                    /* 优先级相同，如果是系统的，则换成中继的 */
                    pstNumTransform = pstNumTransformEntry;

                    continue;
                }
            }
        }
    }

    if (DOS_ADDR_INVALID(pstNumTransform))
    {
        /* 没有找到合适的变换规则 */
        sc_logr_debug(SC_DIALER, "Not find number transfer rule for the task %d, timing is : %d, number select : %d"
                                , pstSCB->ulTaskID, enTiming, enNumSelect);

        goto succ;
    }

    sc_logr_debug(SC_DIALER, "Find a number transfer rule(%d) for the task %d, timing is : %d, number select : %d"
                                , pstNumTransform->ulID, pstSCB->ulTaskID, enTiming, enNumSelect);

    if (SC_NUM_TRANSFORM_SELECT_CALLER == pstNumTransform->enNumSelect)
    {
        dos_strncpy(szNeedTransformNum, pstSCB->szCallerNum, SC_TEL_NUMBER_LENGTH);
    }
    else
    {
        dos_strncpy(szNeedTransformNum, pstSCB->szCalleeNum, SC_TEL_NUMBER_LENGTH);
    }

    szNeedTransformNum[SC_TEL_NUMBER_LENGTH - 1] = '\0';

    /* 根据找到的规则变换号码 */
    if (pstNumTransform->bReplaceAll)
    {
        /* 完全替代 */
        if (pstNumTransform->szReplaceNum[0] == '*')
        {
            /* TODO 使用号码组中的号码 */
            if (SC_NUM_TRANSFORM_OBJECT_CUSTOMER != pstNumTransform->enObject)
            {
                /* 只有企业客户，才可以选择号码组中的号码进行替换 */
                sc_logr_info(SC_DIALER, "Number transfer rule(%d) for the task %d fail : only a enterprise customers can, choose number in the group number"
                                , pstNumTransform->ulID, pstSCB->ulTaskID, enTiming, enNumSelect);

                goto fail;
            }
        }
        else if (pstNumTransform->szReplaceNum[0] == '\0')
        {
            /* 完全替换的号码不能为空 */
            sc_logr_info(SC_DIALER, "The number transfer rule(%d) replace num is NULL!"
                                , pstNumTransform->ulID, pstSCB->ulTaskID, enTiming, enNumSelect);

            goto fail;
        }
        else
        {
            dos_strcpy(szNeedTransformNum, pstNumTransform->szReplaceNum);
        }

        goto succ;
    }

    /* 删除左边几位 */
    ulOffsetLen = pstNumTransform->ulDelLeft;
    if (ulOffsetLen != 0)
    {
        ulNumLen = dos_strlen(szNeedTransformNum);

        if (ulOffsetLen >= ulNumLen)
        {
            /* 删除的位数大于号码的长度，整个号码置空 */
            szNeedTransformNum[0] = '\0';
        }
        else
        {
            for (ulIndex=ulOffsetLen; ulIndex<=ulNumLen; ulIndex++)
            {
                szNeedTransformNum[ulIndex-ulOffsetLen] = szNeedTransformNum[ulIndex];
            }
        }
    }

    /* 删除右边几位 */
    ulOffsetLen = pstNumTransform->ulDelLeft;
    if (ulOffsetLen != 0)
    {
        ulNumLen = dos_strlen(szNeedTransformNum);

        if (ulOffsetLen >= ulNumLen)
        {
            /* 删除的位数大于号码的长度，整个号码置空 */
            szNeedTransformNum[0] = '\0';
        }
        else
        {
            szNeedTransformNum[ulNumLen-ulOffsetLen] = '\0';
        }
    }

    /* 增加前缀 */
    if (pstNumTransform->szAddPrefix[0] != '\0')
    {
        ulNumLen = dos_strlen(szNeedTransformNum);
        ulOffsetLen = dos_strlen(pstNumTransform->szAddPrefix);
        if (ulNumLen + ulOffsetLen >= SC_TEL_NUMBER_LENGTH)
        {
            /* 超过号码的长度，失败 */

            goto fail;
        }

        for (lIndex=ulNumLen; lIndex>=0; lIndex--)
        {
            szNeedTransformNum[lIndex+ulOffsetLen] = szNeedTransformNum[lIndex];
        }

        dos_strncpy(szNeedTransformNum, pstNumTransform->szAddPrefix, ulOffsetLen);
    }
    /* 增加后缀 */
    if (pstNumTransform->szAddSuffix[0] != '\0')
    {
        ulNumLen = dos_strlen(szNeedTransformNum);
        ulOffsetLen = dos_strlen(pstNumTransform->szAddPrefix);
        if (ulNumLen + ulOffsetLen >= SC_TEL_NUMBER_LENGTH)
        {
            /* 超过号码的长度，失败 */

            goto fail;
        }

        dos_strcat(szNeedTransformNum, pstNumTransform->szAddSuffix);
    }

    if (szNeedTransformNum[0] == '\0')
    {
        goto fail;
    }
    szNeedTransformNum[SC_TEL_NUMBER_LENGTH - 1] = '\0';

succ:

    if (DOS_ADDR_INVALID(pstNumTransform))
    {
        pthread_mutex_unlock(&g_mutexNumTransformList);

        return DOS_SUCC;
    }

    if (SC_NUM_TRANSFORM_SELECT_CALLER == pstNumTransform->enNumSelect)
    {
        sc_logr_debug(SC_DIALER, "The number transfer(%d) SUCC, task : %d, befor : %s ,after : %s", pstNumTransform->ulID, pstSCB->ulTaskID, pstSCB->szCallerNum, szNeedTransformNum);
        dos_strcpy(pstSCB->szCallerNum, szNeedTransformNum);
    }
    else
    {
        sc_logr_debug(SC_DIALER, "The number transfer(%d) SUCC, task : %d, befor : %s ,after : %s", pstNumTransform->ulID, pstSCB->ulTaskID, pstSCB->szCalleeNum, szNeedTransformNum);
        dos_strcpy(pstSCB->szCalleeNum, szNeedTransformNum);
    }

    pthread_mutex_unlock(&g_mutexNumTransformList);

    return DOS_SUCC;

fail:

    sc_logr_info(SC_DIALER, "the number transfer(%d) FAIL, task : %d", pstNumTransform->ulID, pstSCB->ulTaskID);
    if (SC_NUM_TRANSFORM_SELECT_CALLER == pstNumTransform->enNumSelect)
    {
        pstSCB->szCallerNum[0] = '\0';
    }
    else
    {
        pstSCB->szCalleeNum[0] = '\0';
    }

    pthread_mutex_unlock(&g_mutexNumTransformList);

    return DOS_FAIL;
}

U32 sc_ep_get_eix_by_tt(S8 *pszTTNumber, S8 *pszEIX, U32 ulLength)
{
    U32            ulHashIndex   = 0;
    HASH_NODE_S    *pstHashNode  = NULL;
    SC_TT_NODE_ST  *pstTTNumber  = NULL;
    SC_TT_NODE_ST  *pstTTNumberLast  = NULL;

    if (DOS_ADDR_INVALID(pszTTNumber)
        || DOS_ADDR_INVALID(pszEIX)
        || ulLength <= 0)
    {
        DOS_ASSERT(0);
        return DOS_FAIL;
    }

    pthread_mutex_lock(&g_mutexHashTTNumber);
    HASH_Scan_Table(g_pstHashTTNumber, ulHashIndex)
    {
        HASH_Scan_Bucket(g_pstHashTTNumber, ulHashIndex, pstHashNode, HASH_NODE_S*)
        {
            if (DOS_ADDR_INVALID(pstHashNode ) || DOS_ADDR_INVALID(pstHashNode->pHandle))
            {
                continue;
            }

            pstTTNumber = pstHashNode->pHandle;
            /* 匹配前缀 */
            if (0 == dos_strncmp(pszTTNumber
                , pstTTNumber->szPrefix
                , dos_strlen(pstTTNumber->szPrefix)))
            {
                /* 要做一个最优匹配(匹配长度越大越优) */
                if (pstTTNumberLast)
                {
                    if (dos_strlen(pstTTNumberLast->szPrefix) < dos_strlen(pstTTNumber->szPrefix))
                    {
                        pstTTNumberLast = pstTTNumber;
                    }
                }
                else
                {
                    pstTTNumberLast = pstTTNumber;
                }
            }
        }
    }

    if (pstTTNumberLast)
    {
        dos_snprintf(pszEIX, ulLength, "%s", pstTTNumberLast->szAddr);
    }
    else
    {
        pthread_mutex_unlock(&g_mutexHashTTNumber);

        sc_logr_info(SC_ESL, "Cannot find the EIA for the TT number %s", pszTTNumber);
        return DOS_FAIL;
    }

    pthread_mutex_unlock(&g_mutexHashTTNumber);

    sc_logr_info(SC_ESL, "Found the EIA(%s) for the TT number(%s).", pszEIX, pszTTNumber);
    return DOS_SUCC;
}

/**
 * 函数: U32 sc_ep_esl_execute(const S8 *pszApp, const S8 *pszArg, const S8 *pszUUID)
 * 功能: 使用pstHandle所指向的ESL句柄指向命令pszApp，参数为pszArg，对象为pszUUID
 * 参数:
 *      esl_handle_t *pstHandle: ESL句柄
 *      const S8 *pszApp: 执行的命令
 *      const S8 *pszArg: 命令参数
 *      const S8 *pszUUID: channel的UUID
 * 返回值: 成功返回DOS_SUCC，否则返回DOS_FAIL
 *
 * 注意: 当该函数在执行命令时，如果发现当前句柄已经失去连接，将会重新连接ESL服务器
 */
U32 sc_ep_esl_execute(const S8 *pszApp, const S8 *pszArg, const S8 *pszUUID)
{
    U32 ulRet;

    if (DOS_ADDR_INVALID(pszApp)
        || DOS_ADDR_INVALID(pszUUID))
    {
        DOS_ASSERT(0);

        return DOS_FAIL;
    }

    if (!g_pstHandle->stSendHandle.connected)
    {
        ulRet = esl_connect(&g_pstHandle->stSendHandle, "127.0.0.1", 8021, NULL, "ClueCon");
        if (ESL_SUCCESS != ulRet)
        {
            esl_disconnect(&g_pstHandle->stSendHandle);
            sc_logr_notice(SC_ESL, "ELS for send event re-connect fail, return code:%d, Msg:%s. Will be retry after 1 second.", ulRet, g_pstHandle->stSendHandle.err);

            DOS_ASSERT(0);

            return DOS_FAIL;
        }

        g_pstHandle->stSendHandle.event_lock = 1;
    }

    if (ESL_SUCCESS != esl_execute(&g_pstHandle->stSendHandle, pszApp, pszArg, pszUUID))
    {
        DOS_ASSERT(0);
        sc_logr_notice(SC_ESL, "ESL execute command fail. Result:%d, APP: %s, ARG : %s, UUID: %s"
                        , ulRet
                        , pszApp
                        , DOS_ADDR_VALID(pszArg) ? pszArg : "NULL"
                        , DOS_ADDR_VALID(pszUUID) ? pszUUID : "NULL");

        return DOS_FAIL;
    }

    sc_logr_debug(SC_ESL, "ESL execute command SUCC. APP: %s, Param: %s"
                    , pszApp
                    , DOS_ADDR_VALID(pszArg) ? pszArg : "NULL");

    return DOS_SUCC;
}

/**
 * 函数: U32 sc_ep_esl_execute_cmd(const S8 *pszCmd)
 * 功能: 使用pstHandle所指向的ESL句柄执行命令
 * 参数:
 *      const S8 *pszCmd:
 * 返回值: 成功返回DOS_SUCC，否则返回DOS_FAIL
 *
 * 注意: 当该函数在执行命令时，如果发现当前句柄已经失去连接，将会重新连接ESL服务器
 */
U32 sc_ep_esl_execute_cmd(const S8 *pszCmd)
{
    U32 ulRet;

    if (DOS_ADDR_INVALID(pszCmd))
    {
        DOS_ASSERT(0);

        return DOS_FAIL;
    }

    if (!g_pstHandle->stSendHandle.connected)
    {
        ulRet = esl_connect(&g_pstHandle->stSendHandle, "127.0.0.1", 8021, NULL, "ClueCon");
        if (ESL_SUCCESS != ulRet)
        {
            esl_disconnect(&g_pstHandle->stSendHandle);
            sc_logr_notice(SC_ESL, "ELS for send event re-connect fail, return code:%d, Msg:%s. Will be retry after 1 second.", ulRet, g_pstHandle->stSendHandle.err);

            DOS_ASSERT(0);

            return DOS_FAIL;
        }

        g_pstHandle->stSendHandle.event_lock = 1;
    }

    if (ESL_SUCCESS != esl_send(&g_pstHandle->stSendHandle, pszCmd))
    {
        DOS_ASSERT(0);
        sc_logr_notice(SC_ESL, "ESL execute command fail. Result:%d, CMD: %s"
                        , ulRet
                        , pszCmd);

        return DOS_FAIL;
    }

    sc_logr_notice(SC_ESL, "ESL execute command SUCC. CMD: %s", pszCmd);

    return DOS_SUCC;
}

U32 sc_ep_hangup_call(SC_SCB_ST *pstSCB, U32 ulTernmiteCase)
{
    if (DOS_ADDR_INVALID(pstSCB))
    {
        DOS_ASSERT(0);

        return DOS_FAIL;
    }

    if (ulTernmiteCase >= BS_TERM_BUTT )
    {
        DOS_ASSERT(0);

        return DOS_FAIL;
    }

    if ('\0' == pstSCB->szUUID[0])
    {
        DOS_ASSERT(0);

        return DOS_FAIL;
    }

    sc_logr_info(SC_ESL, "Hangup call with error code %d, pstscb : %d, other : %d", ulTernmiteCase, pstSCB->usSCBNo, pstSCB->usOtherSCBNo);

    sc_ep_esl_execute("hangup", NULL, pstSCB->szUUID);
    pstSCB->ucTerminationCause = ulTernmiteCase;
    pstSCB->ucTerminationFlag = DOS_TRUE;

    return DOS_SUCC;
}



/**
 * 函数: U32 sc_ep_parse_event(esl_event_t *pstEvent, SC_SCB_ST *pstSCB)
 * 功能: 从ESL事件pstEvent中获取参数，并存储在pstSCB
 * 参数:
 *          esl_event_t *pstEvent : 数据源 ESL事件
 *          SC_SCB_ST *pstSCB     : SCB，存储数据
 * 返回值: 成功返回DOS_SUCC，否则返回DOS_FAIL
 */
U32 sc_ep_parse_event(esl_event_t *pstEvent, SC_SCB_ST *pstSCB)
{
    S8         *pszCaller    = NULL;
    S8         *pszCallee    = NULL;
    S8         *pszANI       = NULL;
    S8         *pszCallSrc   = NULL;
    S8         *pszTrunkIP   = NULL;
    S8         *pszGwName    = NULL;
    S8         *pszCallDirection = NULL;
    S8         *pszOtherLegUUID  = NULL;
    SC_SCB_ST  *pstSCB2 = NULL;

    SC_TRACE_IN(pstEvent, pstSCB, 0, 0);

    if (DOS_ADDR_INVALID(pstEvent) || DOS_ADDR_INVALID(pstSCB))
    {
        DOS_ASSERT(0);

        SC_TRACE_OUT();
        return DOS_SUCC;
    }

    /* 从ESL EVENT中回去相关呼叫信息 */
    /*
     * 1. PSTN呼入
     *   特征:
     *       Call Direction: Inbound;
     *       Profile Name:   external
     *   获取的信息:
     *       对端IP或者gateway name获取呼叫的网关ID
     *       主被叫信息, 来电信信息
     * 2. 呼出到PSTN
     *   特征:
     *       Call Direction: outbount;
     *       Profile Name:   external;
     *   获取的信息:
     *       对端IP或者gateway name获取呼叫的网关ID
     *       主被叫信息, 来电信信息
     *       获取用户信息标示
     * 3. 内部呼叫
     *   特征:
     *       Call Direction: Inbound;
     *       Profile Name:   internal;
     *   获取的信息:
     *       主被叫信息, 来电信信息
     *       获取用户信息标示
     */

    pszCallSrc = esl_event_get_header(pstEvent, "variable_sofia_profile_name");
    if (DOS_ADDR_INVALID(pszCallSrc))
    {
        DOS_ASSERT(0);

        SC_TRACE_OUT();
        return DOS_FAIL;
    }

    pszCallDirection = esl_event_get_header(pstEvent, "Call-Direction");
    if (DOS_ADDR_INVALID(pszCallDirection))
    {
        DOS_ASSERT(0);

        SC_TRACE_OUT();
        return DOS_FAIL;
    }

    pszGwName = esl_event_get_header(pstEvent, "variable_sip_gateway_name");
    pszTrunkIP = esl_event_get_header(pstEvent, "Caller-Network-Addr");
    pszCaller = esl_event_get_header(pstEvent, "Caller-Caller-ID-Number");
    pszCallee = esl_event_get_header(pstEvent, "Caller-Destination-Number");
    pszANI    = esl_event_get_header(pstEvent, "Caller-ANI");
    pszOtherLegUUID = esl_event_get_header(pstEvent, "Other-Leg-Unique-ID");
    if (DOS_ADDR_INVALID(pszCaller) || DOS_ADDR_INVALID(pszCallee) || DOS_ADDR_INVALID(pszANI))
    {
        DOS_ASSERT(0);

        SC_TRACE_OUT();
        return DOS_FAIL;
    }

    if (DOS_ADDR_VALID(pszOtherLegUUID))
    {
        pstSCB2 = sc_scb_hash_tables_find(pszOtherLegUUID);
    }

    /* 将相关数据写入SCB中 */
    pthread_mutex_lock(&pstSCB->mutexSCBLock);
    if (DOS_ADDR_VALID(pstSCB2))
    {
        pstSCB->usOtherSCBNo = pstSCB2->usSCBNo;
        pstSCB2->usOtherSCBNo = pstSCB->usSCBNo;
    }
    dos_strncpy(pstSCB->szCalleeNum, pszCallee, sizeof(pstSCB->szCalleeNum));
    pstSCB->szCalleeNum[sizeof(pstSCB->szCalleeNum) -1] = '\0';
    dos_strncpy(pstSCB->szCallerNum, pszCaller, sizeof(pstSCB->szCallerNum));
    pstSCB->szCallerNum[sizeof(pstSCB->szCallerNum) -1] = '\0';
    dos_strncpy(pstSCB->szANINum, pszANI, sizeof(pstSCB->szANINum));
    pstSCB->szANINum[sizeof(pstSCB->szANINum) -1] = '\0';
    pthread_mutex_unlock(&pstSCB->mutexSCBLock);

    SC_TRACE_OUT();
    return DOS_SUCC;
}

U32 sc_ep_parse_extra_data(esl_event_t *pstEvent, SC_SCB_ST *pstSCB)
{
    S8 *pszTmp = NULL;
    U64 uLTmp  = 0;

    if (DOS_ADDR_INVALID(pstEvent)
        || DOS_ADDR_INVALID(pstSCB)
        || DOS_ADDR_INVALID(pstSCB->pstExtraData))
    {
        DOS_ASSERT(0);
        return DOS_FAIL;
    }

    pszTmp = esl_event_get_header(pstEvent, "Caller-Channel-Created-Time");
    if (DOS_ADDR_VALID(pszTmp)
        && '\0' != pszTmp[0]
        && dos_atoull(pszTmp, &uLTmp) == 0)
    {
        pstSCB->pstExtraData->ulStartTimeStamp = uLTmp / 1000000;
        sc_logr_debug(SC_ESL, "Get extra data: Caller-Channel-Created-Time=%s(%u)", pszTmp, pstSCB->pstExtraData->ulStartTimeStamp);
    }

    pszTmp = esl_event_get_header(pstEvent, "Caller-Channel-Answered-Time");
    if (DOS_ADDR_VALID(pszTmp)
        && '\0' != pszTmp[0]
        && dos_atoull(pszTmp, &uLTmp) == 0)
    {
        pstSCB->pstExtraData->ulAnswerTimeStamp = uLTmp / 1000000;
        sc_logr_debug(SC_ESL, "Get extra data: Caller-Channel-Answered-Time=%s(%u)", pszTmp, pstSCB->pstExtraData->ulAnswerTimeStamp);
    }

    pszTmp = esl_event_get_header(pstEvent, "Caller-Channel-Progress-Time");
    if (DOS_ADDR_VALID(pszTmp)
        && '\0' != pszTmp[0]
        && dos_atoull(pszTmp, &uLTmp) == 0)
    {
        pstSCB->pstExtraData->ulRingTimeStamp = uLTmp / 1000000;
        sc_logr_debug(SC_ESL, "Get extra data: Caller-Channel-Progress-Time=%s(%u)", pszTmp, pstSCB->pstExtraData->ulRingTimeStamp);
    }

    pszTmp = esl_event_get_header(pstEvent, "Caller-Channel-Progress-Media-Time");
    if (DOS_ADDR_VALID(pszTmp)
        && '\0' != pszTmp[0]
        && dos_atoull(pszTmp, &uLTmp) == 0)
    {
        pstSCB->pstExtraData->ulBridgeTimeStamp = uLTmp / 1000000;
        sc_logr_debug(SC_ESL, "Get extra data: Caller-Channel-Progress-Media-Time=%s(%u)", pszTmp, pstSCB->pstExtraData->ulBridgeTimeStamp);
    }

    pszTmp = esl_event_get_header(pstEvent, "Caller-Channel-Hangup-Time");
    if (DOS_ADDR_VALID(pszTmp)
        && '\0' != pszTmp[0]
        && dos_atoull(pszTmp, &uLTmp) == 0)
    {
        pstSCB->pstExtraData->ulByeTimeStamp = uLTmp / 1000000;
        sc_logr_debug(SC_ESL, "Get extra data: Caller-Channel-Hangup-Time=%s(%u)", pszTmp, pstSCB->pstExtraData->ulByeTimeStamp);
    }

    return DOS_SUCC;
}

U32 sc_ep_terminate_call(SC_SCB_ST *pstSCB)
{
    SC_SCB_ST *pstSCBOther = NULL;

    if (DOS_ADDR_INVALID(pstSCB))
    {
        DOS_ASSERT(0);
        return DOS_FAIL;
    }

    pstSCBOther = sc_scb_get(pstSCB->usOtherSCBNo);
    if (DOS_ADDR_VALID(pstSCBOther))
    {
        if ('\0' != pstSCBOther->szUUID[0])
        {
            sc_ep_esl_execute("hangup", NULL, pstSCB->szUUID);
            sc_logr_notice(SC_ESL, "Hangup Call for Auth FAIL. SCB No : %d, UUID: %d.", pstSCBOther->usSCBNo, pstSCBOther->szUUID);
        }
        else
        {
            SC_SCB_SET_STATUS(pstSCBOther, SC_SCB_RELEASE);
            sc_call_trace(pstSCBOther, "Terminate call.");
            sc_logr_notice(SC_ESL, "Call terminate call. SCB No : %d.", pstSCBOther->usSCBNo);
            DOS_ASSERT(0);
            sc_scb_free(pstSCBOther);
        }
    }

    if ('\0' != pstSCB->szUUID[0])
    {
        /* 有FS通讯的话，就直接挂断呼叫就好 */
        sc_ep_esl_execute("hangup", NULL, pstSCB->szUUID);
        sc_logr_notice(SC_ESL, "Hangup Call for Auth FAIL. SCB No : %d, UUID: %d. *", pstSCB->usSCBNo, pstSCB->szUUID);
    }
    else
    {
        SC_SCB_SET_STATUS(pstSCB, SC_SCB_RELEASE);
        sc_call_trace(pstSCB, "Terminate call.");
        sc_logr_notice(SC_ESL, "Call terminate call. SCB No : %d. *", pstSCB->usSCBNo);
        DOS_ASSERT(0);
        sc_scb_free(pstSCB);
    }

    return DOS_SUCC;
}


/**
 * 函数: U32 sc_ep_internal_service_check(esl_event_t *pstEvent)
 * 功能: 检查当前事件是否是在执行内部业务
 * 参数:
 *          esl_event_t *pstEvent : 数据源 ESL事件
 * 返回值: 成功返回内部业务枚举值，否则返回无效业务枚举
 */
U32 sc_ep_internal_service_check(esl_event_t *pstEvent)
{
    return SC_INTER_SRV_BUTT;
}

/**
 * 函数: BOOL sc_ep_check_extension(S8 *pszNum, U32 ulCustomerID)
 * 功能: 检查pszNum所执行的分机号，是否输入编号为ulCustomerID的客户
 * 参数:
 *      S8 *pszNum: 分机号
 *      U32 ulCustomerID: 客户ID
 * 返回值: 成功返回DOS_TRUE，否则返回DOS_FALSE
 */
BOOL sc_ep_check_extension(S8 *pszNum, U32 ulCustomerID)
{
    SC_USER_ID_NODE_ST *pstUserIDNode = NULL;
    HASH_NODE_S        *pstHashNode   = NULL;
    U32                ulHashIndex    = 0;

    if (DOS_ADDR_INVALID(pszNum))
    {
        DOS_ASSERT(0);

        return DOS_FALSE;
    }

    pthread_mutex_lock(&g_mutexHashSIPUserID);
    HASH_Scan_Table(g_pstHashSIPUserID, ulHashIndex)
    {
        HASH_Scan_Bucket(g_pstHashSIPUserID, ulHashIndex, pstHashNode, HASH_NODE_S*)
        {
            if (DOS_ADDR_INVALID(pstHashNode))
            {
                continue;
            }

            pstUserIDNode = pstHashNode->pHandle;
            if (DOS_ADDR_INVALID(pstUserIDNode))
            {
                continue;
            }

            if (ulCustomerID == pstUserIDNode->ulCustomID
                && 0 == dos_strnicmp(pstUserIDNode->szExtension, pszNum, sizeof(pstUserIDNode->szExtension)))
            {
                pthread_mutex_unlock(&g_mutexHashSIPUserID);
                return DOS_TRUE;
            }
        }
    }
    pthread_mutex_unlock(&g_mutexHashSIPUserID);

    return DOS_FALSE;
}

/**
 * 函数: U32 sc_ep_get_extension_by_userid(S8 *pszUserID, S8 *pszExtension, U32 ulLength)
 * 功能: 获取UserID pszUserID对应的分机号，并copy到缓存pszExtension中，使用ulLength指定缓存的长度
 * 参数:
 *      S8 *pszUserID    : User ID
 *      S8 *pszExtension : 存储分机号的缓存
 *      U32 ulLength     : 缓存长度
 * 返回值: 成功返回DOS_SUCC，否则返回DOS_FAIL
 */
U32 sc_ep_get_extension_by_userid(S8 *pszUserID, S8 *pszExtension, U32 ulLength)
{
    SC_USER_ID_NODE_ST *pstUserIDNode = NULL;
    HASH_NODE_S        *pstHashNode   = NULL;
    U32                ulHashIndex    = 0;

    if (DOS_ADDR_INVALID(pszUserID)
        || DOS_ADDR_INVALID(pszExtension)
        || ulLength <= 0)
    {
        DOS_ASSERT(0);

        return DOS_FAIL;
    }

    pthread_mutex_lock(&g_mutexHashSIPUserID);
    ulHashIndex = sc_sip_userid_hash_func(pszUserID);
    pstHashNode = hash_find_node(g_pstHashSIPUserID, ulHashIndex, (VOID *)pszUserID, sc_ep_sip_userid_hash_find);
    if (DOS_ADDR_INVALID(pstHashNode)
        || DOS_ADDR_INVALID(pstHashNode->pHandle))
    {
        DOS_ASSERT(0);

        pthread_mutex_unlock(&g_mutexHashSIPUserID);
        return DOS_FAIL;
    }

    pstUserIDNode = pstHashNode->pHandle;

    dos_strncpy(pszExtension, pstUserIDNode->szExtension, ulLength);
    pszExtension[ulLength - 1] = '\0';

    pthread_mutex_unlock(&g_mutexHashSIPUserID);

    return DOS_SUCC;
}

/**
 * 函数: U32 sc_ep_get_custom_by_sip_userid(S8 *pszNum)
 * 功能: 获取pszNum所指定UserID所属的客户的ID
 * 参数:
 *      S8 *pszNum    : User ID
 * 返回值: 成功返回客户ID值，否则返回U32_BUTT
 */
U32 sc_ep_get_custom_by_sip_userid(S8 *pszNum)
{
    SC_USER_ID_NODE_ST *pstUserIDNode = NULL;
    HASH_NODE_S        *pstHashNode   = NULL;
    U32                ulHashIndex    = 0;
    U32                ulCustomerID   = 0;

    if (DOS_ADDR_INVALID(pszNum))
    {
        DOS_ASSERT(0);

        return U32_BUTT;
    }

    pthread_mutex_lock(&g_mutexHashSIPUserID);
    ulHashIndex = sc_sip_userid_hash_func(pszNum);
    pstHashNode = hash_find_node(g_pstHashSIPUserID, ulHashIndex, (VOID *)pszNum, sc_ep_sip_userid_hash_find);
    if (DOS_ADDR_INVALID(pstHashNode)
        || DOS_ADDR_INVALID(pstHashNode->pHandle))
    {
        DOS_ASSERT(0);

        pthread_mutex_unlock(&g_mutexHashSIPUserID);
        return U32_BUTT;
    }

    pstUserIDNode = pstHashNode->pHandle;

    ulCustomerID = pstUserIDNode->ulCustomID;
    pthread_mutex_unlock(&g_mutexHashSIPUserID);

    return ulCustomerID;
}

/**
 * 函数: U32 sc_ep_get_custom_by_did(S8 *pszNum)
 * 功能: 通过pszNum所指定的DID号码，找到当前DID号码输入那个客户
 * 参数:
 *      S8 *pszNum : DID号码
 * 返回值: 成功返回客户ID，否则返回U32_BUTT
 */
U32 sc_ep_get_custom_by_did(S8 *pszNum)
{
    SC_DID_NODE_ST     *pstDIDNumNode = NULL;
    HASH_NODE_S        *pstHashNode   = NULL;
    U32                ulHashIndex    = 0;
    U32                ulCustomerID   = 0;

    if (DOS_ADDR_INVALID(pszNum))
    {
        DOS_ASSERT(0);

        return U32_BUTT;
    }

    ulHashIndex = sc_sip_did_hash_func(pszNum);
    pthread_mutex_lock(&g_mutexHashDIDNum);
    pstHashNode = hash_find_node(g_pstHashDIDNum, ulHashIndex, (VOID *)pszNum, sc_ep_did_hash_find);
    if (DOS_ADDR_INVALID(pstHashNode)
        || DOS_ADDR_INVALID(pstHashNode->pHandle))
    {
        DOS_ASSERT(0);

        pthread_mutex_unlock(&g_mutexHashDIDNum);
        return U32_BUTT;
    }

    pstDIDNumNode = pstHashNode->pHandle;
    if (DOS_FALSE == pstDIDNumNode->bValid)
    {
        return U32_BUTT;
    }
    ulCustomerID = pstDIDNumNode->ulCustomID;

    pthread_mutex_unlock(&g_mutexHashDIDNum);

    return ulCustomerID;
}


/**
 * 函数: U32 sc_ep_get_bind_info4did(S8 *pszDidNum, U32 *pulBindType, U32 *pulBindID)
 * 功能: 获取pszDidNum所执行的DID号码的绑定信息
 * 参数:
 *      S8 *pszDidNum    : DID号码
 *      U32 *pulBindType : 当前DID号码绑定的类型
 *      U32 *pulBindID   : 当前DID号码绑定的ID
 * 返回值: 成功返回DOS_SUCC，否则返回DOS_FAIL
 */
U32 sc_ep_get_bind_info4did(S8 *pszDidNum, U32 *pulBindType, U32 *pulBindID)
{
    SC_DID_NODE_ST     *pstDIDNumNode = NULL;
    HASH_NODE_S        *pstHashNode   = NULL;
    U32                ulHashIndex    = 0;

    if (DOS_ADDR_INVALID(pszDidNum)
        || DOS_ADDR_INVALID(pulBindType)
        || DOS_ADDR_INVALID(pulBindID))
    {
        DOS_ASSERT(0);

        return DOS_FAIL;
    }

    ulHashIndex = sc_sip_did_hash_func(pszDidNum);
    pthread_mutex_lock(&g_mutexHashDIDNum);
    pstHashNode = hash_find_node(g_pstHashDIDNum, ulHashIndex, (VOID *)pszDidNum, sc_ep_did_hash_find);
    if (DOS_ADDR_INVALID(pstHashNode)
        || DOS_ADDR_INVALID(pstHashNode->pHandle))
    {
        DOS_ASSERT(0);

        pthread_mutex_unlock(&g_mutexHashDIDNum);
        return DOS_FAIL;
    }

    pstDIDNumNode = pstHashNode->pHandle;

    *pulBindType = pstDIDNumNode->ulBindType;
    *pulBindID = pstDIDNumNode->ulBindID;

    pthread_mutex_unlock(&g_mutexHashDIDNum);

    return DOS_SUCC;
}


/**
 * 函数: U32 sc_ep_get_userid_by_id(U32 ulSIPUserID, S8 *pszUserID, U32 ulLength)
 * 功能: 获取ID为ulSIPUserID SIP User ID，并将SIP USER ID Copy到缓存pszUserID中
 * 参数:
 *      U32 ulSIPUserID : SIP User ID的所以
 *      S8 *pszUserID   : 账户ID缓存
 *      U32 ulLength    : 缓存长度
 * 返回值: 成功返回DOS_SUCC，否则返回DOS_FAIL
 */
U32 sc_ep_get_userid_by_id(U32 ulSipID, S8 *pszUserID, U32 ulLength)
{
    SC_USER_ID_NODE_ST *pstUserIDNode = NULL;
    HASH_NODE_S        *pstHashNode   = NULL;
    U32                ulHashIndex    = 0;

    if (DOS_ADDR_INVALID(pszUserID)
        || ulLength <= 0)
    {
        DOS_ASSERT(0);

        return DOS_FAIL;
    }

    pthread_mutex_lock(&g_mutexHashSIPUserID);
    HASH_Scan_Table(g_pstHashSIPUserID, ulHashIndex)
    {
        HASH_Scan_Bucket(g_pstHashSIPUserID, ulHashIndex, pstHashNode, HASH_NODE_S*)
        {
            if (DOS_ADDR_INVALID(pstHashNode))
            {
                continue;
            }

            pstUserIDNode = pstHashNode->pHandle;
            if (DOS_ADDR_INVALID(pstUserIDNode))
            {
                continue;
            }

            if (ulSipID == pstUserIDNode->ulSIPID)
            {
                dos_strncpy(pszUserID, pstUserIDNode->szUserID, ulLength);
                pszUserID[ulLength - 1] = '\0';
                pthread_mutex_unlock(&g_mutexHashSIPUserID);
                return DOS_SUCC;
            }
        }
    }
    pthread_mutex_unlock(&g_mutexHashSIPUserID);
    return DOS_FAIL;

}

/**
 * 函数: U32 sc_ep_get_userid_by_extension(U32 ulCustomID, S8 *pszExtension, S8 *pszUserID, U32 ulLength)
 * 功能: 获取客户ID为ulCustomID，分机号为pszExtension的User ID，并将User ID Copy到缓存pszUserID中
 * 参数:
 *      U32 ulCustomID  : 客户ID
 *      S8 *pszExtension: 分机号
 *      S8 *pszUserID   : 账户ID缓存
 *      U32 ulLength    : 缓存长度
 * 返回值: 成功返回DOS_SUCC，否则返回DOS_FAIL
 */
U32 sc_ep_get_userid_by_extension(U32 ulCustomID, S8 *pszExtension, S8 *pszUserID, U32 ulLength)
{
    SC_USER_ID_NODE_ST *pstUserIDNode = NULL;
    HASH_NODE_S        *pstHashNode   = NULL;
    U32                ulHashIndex    = 0;

    if (DOS_ADDR_INVALID(pszExtension)
        || DOS_ADDR_INVALID(pszUserID)
        || ulLength <= 0)
    {
        DOS_ASSERT(0);

        return DOS_FAIL;
    }

    pthread_mutex_lock(&g_mutexHashSIPUserID);
    HASH_Scan_Table(g_pstHashSIPUserID, ulHashIndex)
    {
        HASH_Scan_Bucket(g_pstHashSIPUserID, ulHashIndex, pstHashNode, HASH_NODE_S*)
        {
            if (DOS_ADDR_INVALID(pstHashNode))
            {
                continue;
            }

            pstUserIDNode = pstHashNode->pHandle;
            if (DOS_ADDR_INVALID(pstUserIDNode))
            {
                continue;
            }

            if (ulCustomID != pstUserIDNode->ulCustomID)
            {
                continue;
            }

            if (0 != dos_strnicmp(pstUserIDNode->szExtension, pszExtension, sizeof(pstUserIDNode->szExtension)))
            {
                continue;
            }

            dos_strncpy(pszUserID, pstUserIDNode->szUserID, ulLength);
            pszUserID[ulLength - 1] = '\0';
            pthread_mutex_unlock(&g_mutexHashSIPUserID);
            return DOS_SUCC;
        }
    }
    pthread_mutex_unlock(&g_mutexHashSIPUserID);
    return DOS_FAIL;
}

/**
 * 函数: U32 sc_ep_search_route(SC_SCB_ST *pstSCB)
 * 功能: 获取路由组
 * 参数:
 *      SC_SCB_ST *pstSCB : 呼叫控制块，使用主被叫号码
 * 返回值: 成功返回路由组ID，否则返回U32_BUTT
 */
U32 sc_ep_search_route(SC_SCB_ST *pstSCB)
{
    SC_ROUTE_NODE_ST     *pstRouetEntry = NULL;
    DLL_NODE_S           *pstListNode   = NULL;
    struct tm            *pstTime;
    time_t               timep;
    U32                  ulRouteGrpID;
    U32                  ulStartTime, ulEndTime, ulCurrentTime;
    U16                  usCallOutGroup;

    timep = time(NULL);
    pstTime = localtime(&timep);
    if (DOS_ADDR_INVALID(pstTime))
    {
        DOS_ASSERT(0);

        return U32_BUTT;
    }

    ulRouteGrpID = U32_BUTT;

    /* 根据 ulCustomID 查到到 呼出组, 如果查找失败，则将呼出组置为0 */
    if (sc_ep_get_callout_group_by_customerID(pstSCB->ulCustomID, &usCallOutGroup) != DOS_SUCC)
    {
        DOS_ASSERT(0);
        usCallOutGroup = 0;
    }

    pthread_mutex_lock(&g_mutexRouteList);

loop_search:
    DLL_Scan(&g_stRouteList, pstListNode, DLL_NODE_S *)
    {
        pstRouetEntry = (SC_ROUTE_NODE_ST *)pstListNode->pHandle;
        if (DOS_ADDR_INVALID(pstRouetEntry))
        {
            continue;
        }

        if (usCallOutGroup != pstRouetEntry->usCallOutGroup)
        {
            continue;
        }

        sc_logr_info(SC_ESL, "Search Route: %d:%d, %d:%d, %s, %s. Caller:%s, Callee:%s"
                , pstRouetEntry->ucHourBegin, pstRouetEntry->ucMinuteBegin
                , pstRouetEntry->ucHourEnd, pstRouetEntry->ucMinuteEnd
                , pstRouetEntry->szCallerPrefix
                , pstRouetEntry->szCalleePrefix
                , pstSCB->szCallerNum
                , pstSCB->szCalleeNum);

        ulStartTime = pstRouetEntry->ucHourBegin * 60 + pstRouetEntry->ucMinuteBegin;
        ulEndTime = pstRouetEntry->ucHourEnd* 60 + pstRouetEntry->ucMinuteEnd;
        ulCurrentTime = pstTime->tm_hour *60 + pstTime->tm_min;

        if (ulCurrentTime < ulStartTime || ulCurrentTime > ulEndTime)
        {
            sc_logr_info(SC_ESL, "Search Route(FAIL): Time not match: Peroid:%u-:%u, Current:%u"
                    , ulStartTime, ulEndTime, ulCurrentTime);

            continue;
        }

        if ('\0' == pstRouetEntry->szCalleePrefix[0])
        {
            if ('\0' == pstRouetEntry->szCallerPrefix[0])
            {
                ulRouteGrpID = pstRouetEntry->ulID;
                break;
            }
            else
            {
                if (0 == dos_strnicmp(pstRouetEntry->szCallerPrefix, pstSCB->szCallerNum, dos_strlen(pstRouetEntry->szCallerPrefix)))
                {
                    ulRouteGrpID = pstRouetEntry->ulID;
                    break;
                }
            }
        }
        else
        {
            if ('\0' == pstRouetEntry->szCallerPrefix[0])
            {
                if (0 == dos_strnicmp(pstRouetEntry->szCalleePrefix, pstSCB->szCalleeNum, dos_strlen(pstRouetEntry->szCalleePrefix)))
                {
                    ulRouteGrpID = pstRouetEntry->ulID;
                    break;
                }
            }
            else
            {
                if (0 == dos_strnicmp(pstRouetEntry->szCalleePrefix, pstSCB->szCalleeNum, dos_strlen(pstRouetEntry->szCalleePrefix))
                    && 0 == dos_strnicmp(pstRouetEntry->szCallerPrefix, pstSCB->szCallerNum, dos_strlen(pstRouetEntry->szCallerPrefix)))
                {
                    ulRouteGrpID = pstRouetEntry->ulID;
                    break;
                }
            }
        }
    }

    if (U32_BUTT == ulRouteGrpID && usCallOutGroup != 0)
    {
        /* 没有查找到 呼出组一样的 路由， 再循环一遍，查找通配的路由 */
        usCallOutGroup = 0;
        goto loop_search;
    }

    if (DOS_ADDR_VALID(pstRouetEntry))
    {
        sc_logr_debug(SC_ESL, "Search Route Finished. Result: %s, Route ID: %d, Dest Type:%u, Dest ID: %u"
                , U32_BUTT == ulRouteGrpID ? "FAIL" : "SUCC"
                , ulRouteGrpID
                , pstRouetEntry->ulDestType
                , pstRouetEntry->aulDestID[0]);
    }
    else
    {
        sc_logr_debug(SC_ESL, "Search Route Finished. Result: %s, Route ID: %d"
                , U32_BUTT == ulRouteGrpID ? "FAIL" : "SUCC"
                , ulRouteGrpID);
    }

    pthread_mutex_unlock(&g_mutexRouteList);

    return ulRouteGrpID;
}

/**
 * 函数: U32 sc_ep_get_callee_string(U32 ulRouteGroupID, S8 *pszNum, S8 *szCalleeString, U32 ulLength)
 * 功能: 通过路由组ID，和被叫号码获取出局呼叫的呼叫字符串，并将结果存储在szCalleeString中
 * 参数:
 *      U32 ulRouteGroupID : 路由组ID
 *      S8 *pszNum         : 被叫号码
 *      S8 *szCalleeString : 呼叫字符串缓存
 *      U32 ulLength       : 缓存长度
 * 返回值: 成功返回DOS_SUCC，否则返回DOS_FAIL
 */
U32 sc_ep_get_callee_string(U32 ulRouteID, SC_SCB_ST *pstSCB, S8 *szCalleeString, U32 ulLength)
{
    SC_ROUTE_NODE_ST     *pstRouetEntry = NULL;
    DLL_NODE_S           *pstListNode   = NULL;
    DLL_NODE_S           *pstListNode1  = NULL;
    HASH_NODE_S          *pstHashNode   = NULL;
    SC_GW_GRP_NODE_ST    *pstGWGrp      = NULL;
    SC_GW_NODE_ST        *pstGW         = NULL;
    U32                  ulCurrentLen;
    U32                  ulGWCount;
    U32                  ulHashIndex;
    S32                  lIndex;
    BOOL                 blIsFound = DOS_FALSE;
    S8                  *pszNum         = NULL;

    if (DOS_ADDR_INVALID(pstSCB)
        || DOS_ADDR_INVALID(pstSCB->szCalleeNum)
        || DOS_ADDR_INVALID(szCalleeString)
        || ulLength <= 0)
    {
        DOS_ASSERT(0);
        return DOS_FAIL;
    }

    pszNum = pstSCB->szCalleeNum;

    ulCurrentLen = 0;
    pthread_mutex_lock(&g_mutexRouteList);
    DLL_Scan(&g_stRouteList, pstListNode, DLL_NODE_S *)
    {
        pstRouetEntry = (SC_ROUTE_NODE_ST *)pstListNode->pHandle;
        if (DOS_ADDR_INVALID(pstRouetEntry))
        {
            continue;
        }

        if (pstRouetEntry->ulID == ulRouteID)
        {
            switch (pstRouetEntry->ulDestType)
            {
                case SC_DEST_TYPE_GATEWAY:
                    /* TODO 路由后号码变换。现在只支持中继的，不支持中继组 */
                    if (sc_ep_num_transform(pstSCB, pstRouetEntry->aulDestID[0], SC_NUM_TRANSFORM_TIMING_AFTER, SC_NUM_TRANSFORM_SELECT_CALLER) != DOS_SUCC)
                    {
                        DOS_ASSERT(0);
                        blIsFound = DOS_FALSE;

                        break;
                    }

                    if (sc_ep_num_transform(pstSCB, pstRouetEntry->aulDestID[0], SC_NUM_TRANSFORM_TIMING_AFTER, SC_NUM_TRANSFORM_SELECT_CALLEE) != DOS_SUCC)
                    {
                        DOS_ASSERT(0);
                        blIsFound = DOS_FALSE;

                        break;
                    }

                    ulCurrentLen = dos_snprintf(szCalleeString + ulCurrentLen
                                    , ulLength - ulCurrentLen
                                    , "sofia/gateway/%d/%s|"
                                    , pstRouetEntry->aulDestID[0]
                                    , pszNum);

                    blIsFound = DOS_TRUE;
                    break;
                case SC_DEST_TYPE_GW_GRP:
                    /* 查找网关组 */
                    for (lIndex=0; lIndex<SC_ROUT_GW_GRP_MAX_SIZE; lIndex++)
                    {
                        if (U32_BUTT == pstRouetEntry->aulDestID[lIndex])
                        {
                            break;
                        }

                        sc_logr_debug(SC_ESL, "Search gateway froup, ID is %d", pstRouetEntry->aulDestID[lIndex]);
                        ulHashIndex = sc_ep_gw_grp_hash_func(pstRouetEntry->aulDestID[lIndex]);
                        pstHashNode = hash_find_node(g_pstHashGWGrp, ulHashIndex, (VOID *)&pstRouetEntry->aulDestID[lIndex], sc_ep_gw_grp_hash_find);
                        if (DOS_ADDR_INVALID(pstHashNode)
                            || DOS_ADDR_INVALID(pstHashNode->pHandle))
                        {
                            /* 没有找到对应的中继组，继续查找下一个，这种情况，理论上是不应该出现的 */
                            sc_logr_info(SC_ESL, "Not found gateway froup %d", pstRouetEntry->aulDestID[lIndex]);

                            continue;
                        }

                        /* 查找网关 */
                        /* 生成呼叫字符串 */
                        pstGWGrp= pstHashNode->pHandle;
                        ulGWCount = 0;
                        pthread_mutex_lock(&pstGWGrp->mutexGWList);
                        DLL_Scan(&pstGWGrp->stGWList, pstListNode1, DLL_NODE_S *)
                        {
                            if (DOS_ADDR_VALID(pstListNode1)
                                && DOS_ADDR_VALID(pstListNode1->pHandle))
                            {
                                pstGW = pstListNode1->pHandle;
                                if (DOS_FALSE == pstGW->bStatus)
                                {
                                    continue;
                                }
                                ulCurrentLen += dos_snprintf(szCalleeString + ulCurrentLen
                                                , ulLength - ulCurrentLen
                                                , "sofia/gateway/%d/%s|"
                                                , pstGW->ulGWID
                                                , pszNum);

                                ulGWCount++;
                            }
                        }
                        pthread_mutex_unlock(&pstGWGrp->mutexGWList);
                    }

                    if (ulGWCount > 0)
                    {
                        blIsFound = DOS_TRUE;
                    }
                    else
                    {
                        DOS_ASSERT(0);
                        blIsFound = DOS_FALSE;
                    }
                    break;
                default:
                    DOS_ASSERT(0);
                    blIsFound = DOS_FALSE;
                    break;
            }
        }
    }
    pthread_mutex_unlock(&g_mutexRouteList);

    if (blIsFound)
    {
        /* 最后多了一个  | */
        szCalleeString[dos_strlen(szCalleeString) - 1] = '\0';
        sc_logr_debug(SC_ESL, "callee string is %s", szCalleeString);

        return DOS_SUCC;
    }
    else
    {
        szCalleeString[0] = '\0';
        return DOS_FAIL;
    }
}

/**
 * 函数: BOOL sc_ep_dst_is_black(S8 *pszNum)
 * 功能: 判断pszNum所指定的号码是否在黑名单中
 * 参数:
 *      S8 *pszNum : 需要被处理的号码
 * 返回值: 成功返DOS_TRUE，否则返回DOS_FALSE
 */
BOOL sc_ep_dst_is_black(S8 *pszNum)
{
    return DOS_FALSE;
}

/**
 * 函数: U32 sc_ep_get_source(esl_event_t *pstEvent)
 * 功能: 通过esl事件pstEvent判断当前呼叫的来源
 * 参数:
 *      esl_event_t *pstEvent : 需要被处理的时间
 * 返回值: 枚举值 enum tagCallDirection
 */
U32 sc_ep_get_source(esl_event_t *pstEvent)
{
    const S8 *pszCallSource;

    pszCallSource = esl_event_get_header(pstEvent, "variable_sofia_profile_name");
    if (DOS_ADDR_INVALID(pszCallSource))
    {
        DOS_ASSERT(0);
        return SC_DIRECTION_INVALID;
    }

    if (0 == dos_strcmp(pszCallSource, "internal"))
    {
        return SC_DIRECTION_SIP;
    }

    return SC_DIRECTION_PSTN;
}

/**
 * 函数: U32 sc_ep_get_source(esl_event_t *pstEvent)
 * 功能: 通过esl事件pstEvent判断当前呼叫的目的地
 * 参数:
 *      esl_event_t *pstEvent : 需要被处理的时间
 * 返回值: 枚举值 enum tagCallDirection
 */
U32 sc_ep_get_destination(esl_event_t *pstEvent)
{
    S8 *pszDstNum     = NULL;
    S8 *pszSrcNum     = NULL;
    S8 *pszCallSource = NULL;
    U32 ulCustomID    = U32_BUTT;
    U32 ulCustomID1   = U32_BUTT;

    if (DOS_ADDR_INVALID(pstEvent))
    {
        DOS_ASSERT(0);

        return SC_DIRECTION_INVALID;
    }

    pszDstNum = esl_event_get_header(pstEvent, "Caller-Destination-Number");
    pszSrcNum = esl_event_get_header(pstEvent, "Caller-Caller-ID-Number");
    pszCallSource = esl_event_get_header(pstEvent, "variable_sofia_profile_name");
    if (DOS_ADDR_INVALID(pszDstNum)
        || DOS_ADDR_INVALID(pszSrcNum)
        || DOS_ADDR_INVALID(pszCallSource))
    {
        DOS_ASSERT(0);

        return SC_DIRECTION_INVALID;
    }

    if (sc_ep_dst_is_black(pszDstNum))
    {
        sc_logr_notice(SC_ESL, "The destination is in black list. %s", pszDstNum);

        return SC_DIRECTION_INVALID;
    }

    if (0 == dos_strcmp(pszCallSource, "internal"))
    {
        /* IP测发起的呼叫，主叫一定为某SIP账户 */
        ulCustomID = sc_ep_get_custom_by_sip_userid(pszSrcNum);
        if (U32_BUTT == ulCustomID)
        {
            DOS_ASSERT(0);

            sc_logr_info(SC_ESL, "Source number %s is not invalid sip user id. Reject Call", pszSrcNum);
            return DOS_FAIL;
        }

        /*  测试被叫是否是分机号 */
        if (sc_ep_check_extension(pszDstNum, ulCustomID))
        {
            return SC_DIRECTION_SIP;
        }

        /* 被叫号码是否是同一个客户下的SIP User ID */
        ulCustomID1 = sc_ep_get_custom_by_sip_userid(pszDstNum);
        if (ulCustomID == ulCustomID1)
        {
            return SC_DIRECTION_SIP;
        }

        return SC_DIRECTION_PSTN;
    }
    else
    {
        ulCustomID = sc_ep_get_custom_by_did(pszDstNum);
        if (U32_BUTT == ulCustomID)
        {
            DOS_ASSERT(0);

            sc_logr_notice(SC_ESL, "The destination %s is not a DID number. Reject Call.", pszDstNum);
            return SC_DIRECTION_INVALID;
        }

        return SC_DIRECTION_SIP;
    }
}

/**
 * 函数: sc_ep_agent_signin
 * 功能: 坐席长签时，向坐席发起呼叫
 * 参数:
 *      SC_ACD_AGENT_INFO_ST *pstAgentInfo : 坐席信息
 * 返回值: 成功返回DOS_SUCC,失败返回DOS_FAIL
 */
U32 sc_ep_agent_signin(const SC_ACD_AGENT_INFO_ST *pstAgentInfo)
{
    S8            szAPPParam[512] = { 0 };
    U32           ulErrCode = BS_TERM_NONE;
    SC_SCB_ST            *pstSCB = NULL;

    if (DOS_ADDR_INVALID(pstAgentInfo))
    {
        DOS_ASSERT(0);

        return DOS_FAIL;
    }

    sc_logr_info(SC_ESL, "Make call for agent signin, ID: %u, User ID: %s, Extension: %s, Emp NO: %s"
                    , pstAgentInfo->ulSiteID
                    , pstAgentInfo->szUserID
                    , pstAgentInfo->szExtension
                    , pstAgentInfo->szEmpNo);

    pstSCB = sc_scb_alloc();
    if (DOS_ADDR_INVALID(pstSCB))
    {
        DOS_ASSERT(0);

        sc_logr_error(SC_ESL, "%s", "Allc SCB FAIL.");
        ulErrCode = BS_TERM_SCB_LEEK;
        goto proc_error;
    }

    pstSCB->ulCustomID = pstAgentInfo->ulCustomerID;
    pstSCB->ulAgentID = pstAgentInfo->ulSiteID;
    pstSCB->ucLegRole = SC_CALLEE;
    pstSCB->bRecord = pstAgentInfo->bRecord;
    pstSCB->bIsAgentCall = DOS_TRUE;

    switch (pstAgentInfo->ucBindType)
    {
        case AGENT_BIND_SIP:
            dos_strncpy(pstSCB->szCalleeNum, pstAgentInfo->szUserID, sizeof(pstSCB->szCalleeNum));
            pstSCB->szCalleeNum[sizeof(pstSCB->szCalleeNum) - 1] = '\0';
            break;
        case AGENT_BIND_TELE:
            dos_strncpy(pstSCB->szCalleeNum, pstAgentInfo->szTelePhone, sizeof(pstSCB->szCalleeNum));
            pstSCB->szCalleeNum[sizeof(pstSCB->szCalleeNum) - 1] = '\0';
            break;
        case AGENT_BIND_MOBILE:
            dos_strncpy(pstSCB->szCalleeNum, pstAgentInfo->szMobile, sizeof(pstSCB->szCalleeNum));
            pstSCB->szCalleeNum[sizeof(pstSCB->szCalleeNum) - 1] = '\0';
            break;
    }

    dos_strncpy(pstSCB->szCallerNum, pstAgentInfo->szUserID, sizeof(pstSCB->szCallerNum));
    pstSCB->szCallerNum[sizeof(pstSCB->szCallerNum) - 1] = '\0';

    dos_strncpy(pstSCB->szSiteNum, pstAgentInfo->szEmpNo, sizeof(pstSCB->szSiteNum));
    pstSCB->szSiteNum[sizeof(pstSCB->szSiteNum) - 1] = '\0';

    SC_SCB_SET_SERVICE(pstSCB, SC_SERV_AGENT_SIGNIN);

    if (AGENT_BIND_SIP != pstAgentInfo->ucBindType)
    {
        SC_SCB_SET_SERVICE(pstSCB, SC_SERV_OUTBOUND_CALL);
        SC_SCB_SET_SERVICE(pstSCB, SC_SERV_EXTERNAL_CALL);

        if (!sc_ep_black_list_check(pstSCB->ulCustomID, pstSCB->szCalleeNum))
        {
            DOS_ASSERT(0);
            sc_logr_info(SC_ESL, "Cannot make call for number %s which is in black list.", pstSCB->szCalleeNum);
            goto proc_error;
        }

        if (sc_send_usr_auth2bs(pstSCB) != DOS_SUCC)
        {
            sc_logr_notice(SC_ESL, "Send auth msg FAIL. SCB No: %d", pstSCB->usSCBNo);
            ulErrCode = BS_TERM_COMM_FAIL;
            goto proc_error;
        }

        return DOS_SUCC;
    }

    SC_SCB_SET_STATUS(pstSCB, SC_SCB_EXEC);

    SC_SCB_SET_SERVICE(pstSCB, SC_SERV_OUTBOUND_CALL);
    SC_SCB_SET_SERVICE(pstSCB, SC_SERV_INTERNAL_CALL);

    dos_snprintf(szAPPParam, sizeof(szAPPParam)
                    , "{scb_number=%u,other_leg_scb=%u,main_service=%d,origination_caller_id_number=%s,origination_caller_id_name=%s}user/%s"
                    , pstSCB->usSCBNo
                    , pstSCB->usOtherSCBNo
                    , SC_SERV_AGENT_SIGNIN
                    , pstSCB->szCalleeNum
                    , pstSCB->szCalleeNum
                    , pstAgentInfo->szUserID);

    if (sc_ep_esl_execute("bridge", szAPPParam, pstSCB->szUUID) != DOS_SUCC)
    {
        /* @TODO 用户体验优化 */
        ulErrCode = BS_TERM_COMM_FAIL;
        goto proc_error;
    }
    else
    {
        /* @TODO 优化  先放音，再打坐席，坐席接通之后再连接到坐席 */
        sc_acd_agent_update_status(pstAgentInfo->ulSiteID, SC_ACD_BUSY, pstSCB->usSCBNo);

        sc_ep_esl_execute("sleep", "1000", pstSCB->szUUID);
        sc_ep_esl_execute("speak", "flite|kal|Is to connect you with an agent, please wait.", pstSCB->szUUID);
    }

    return DOS_SUCC;

proc_error:
    if (pstSCB)
    {
        DOS_ASSERT(0);
        sc_scb_free(pstSCB);
        pstSCB = NULL;
    }

    /* 连接失败，直接结束吧 要不然可能会逻辑上的死循环 */
    sc_acd_update_agent_status(SC_ACD_SITE_ACTION_CONNECT_FAIL, pstSCB->ulAgentID);

    return DOS_FAIL;
}

U32 sc_ep_agent_record(SC_SCB_ST * pstSCB)
{
    S8 szAPPParam[256] = { 0 };
    S8 szFilePath[256] = { 0 };

    if (DOS_ADDR_INVALID(pstSCB))
    {
        DOS_ASSERT(0);
        return DOS_FAIL;
    }

    sc_get_record_file_path(szFilePath, sizeof(szFilePath), pstSCB->ulCustomID, pstSCB->szCallerNum, pstSCB->szCalleeNum);
    pthread_mutex_lock(&pstSCB->mutexSCBLock);
    pstSCB->pszRecordFile = dos_dmem_alloc(dos_strlen(szFilePath) + 1);
    if (DOS_ADDR_VALID(pstSCB->pszRecordFile))
    {
        dos_strncpy(pstSCB->pszRecordFile, szFilePath, dos_strlen(szFilePath) + 1);
        pstSCB->pszRecordFile[dos_strlen(szFilePath)] = '\0';

        dos_snprintf(szAPPParam, sizeof(szAPPParam)
                        , "bgapi uuid_record %s start %s/%s\r\n"
                        , pstSCB->szUUID
                        , SC_RECORD_FILE_PATH
                        , szFilePath);
        sc_ep_esl_execute_cmd(szAPPParam);
        sc_ep_esl_execute("sleep", "300", pstSCB->szUUID);
    }
    else
    {
        DOS_ASSERT(0);
        goto proc_fail;
    }
    pthread_mutex_unlock(&pstSCB->mutexSCBLock);

    return DOS_SUCC;
proc_fail:

    return DOS_FAIL;
}

U32 sc_ep_transfer_notify_release(SC_SCB_ST * pstSCBNotify)
{
    SC_SCB_ST* pstSCBSubscription = NULL;
    SC_SCB_ST* pstSCBPublish = NULL;
    S8         szBuffCMD[512] = { 0 };

    if (!SC_SCB_IS_VALID(pstSCBNotify))
    {
        DOS_ASSERT(0);
        return DOS_FAIL;
    }

    pstSCBPublish = sc_scb_get(pstSCBNotify->usPublishSCB);
    if (!SC_SCB_IS_VALID(pstSCBPublish))
    {
        DOS_ASSERT(0);
        return DOS_FAIL;
    }

    pstSCBSubscription = sc_scb_get(pstSCBNotify->usOtherSCBNo);
    if (!SC_SCB_IS_VALID(pstSCBSubscription))
    {
        DOS_ASSERT(0);
        return DOS_FAIL;
    }

    if (pstSCBPublish->ucStatus < SC_SCB_ACTIVE)
    {
        if (SC_SCB_ACTIVE == pstSCBSubscription->ucStatus)
        {
            if (sc_call_check_service(pstSCBPublish, SC_SERV_BLIND_TRANSFER))
            {
                /* 维护当前业务控制块的状态为release */
                SC_SCB_SET_SERVICE(pstSCBNotify, SC_SCB_RELEASE);
            }
            else if (sc_call_check_service(pstSCBPublish, SC_SERV_ATTEND_TRANSFER))
            {
                /* 要求发布方挂断，要求订阅方挂断 */
                sc_ep_esl_execute("hangup", "", pstSCBSubscription->szUUID);

                /* 维护当前业务控制块的状态为release */
                SC_SCB_SET_SERVICE(pstSCBNotify, SC_SCB_RELEASE);
            }
            else
            {
                DOS_ASSERT(0);
            }
        }
        else /* Release */
        {
            /* 要求发布方挂断(这个地方没办法发送，发布方还没有接通，那就等发布方接通的时候在处理) */

            /* 维护当前业务控制块的状态为release */
            SC_SCB_SET_SERVICE(pstSCBNotify, SC_SCB_RELEASE);
        }
    }
    else if (SC_SCB_ACTIVE == pstSCBPublish->ucStatus)
    {
        if (SC_SCB_ACTIVE == pstSCBSubscription->ucStatus)
        {
            /* 接通订阅方和发布方 */
            dos_snprintf(szBuffCMD, sizeof(szBuffCMD), "uuid_bridge %s %s \r\n", pstSCBSubscription->szUUID, pstSCBPublish->szUUID);
            sc_ep_esl_execute_cmd(szBuffCMD);

            /* 维护各种状态，使后续呼叫与transfer流程无关 */
            pstSCBPublish->usOtherSCBNo = pstSCBSubscription->usSCBNo;
            pstSCBSubscription->usOtherSCBNo = pstSCBPublish->usSCBNo;
            pstSCBPublish->ucTranforRole = SC_TRANS_ROLE_BUTT;
            pstSCBSubscription->ucTranforRole = SC_TRANS_ROLE_BUTT;
            pstSCBNotify->ucTranforRole = SC_TRANS_ROLE_BUTT;

            /* 发送话单 */
            pstSCBNotify->usOtherSCBNo = U16_BUTT;
            sc_acd_agent_update_status(pstSCBNotify->ulAgentID, SC_ACD_IDEL, U32_BUTT);
            sc_send_billing_stop2bs(pstSCBNotify);

            /* 清理资源 */
            sc_scb_free(pstSCBNotify);
        }
        else /* Release */
        {
            /* 要求发布方挂断 */
            sc_ep_esl_execute("hangup", "", pstSCBPublish->szUUID);
        }
    }
    else /* Release */
    {
        /* 这个流程为异常流程 */
        /* 应该在发布方挂断时，让后续呼叫不在于transfer有关 */
        DOS_ASSERT(0);
    }

    return DOS_SUCC;
}

U32 sc_ep_transfer_publish_release(SC_SCB_ST * pstSCBPublish)
{
    SC_SCB_ST* pstSCBSubscription = NULL;
    SC_SCB_ST* pstSCBNotify = NULL;
    S8         szBuffCMD[512] = { 0 };

    if (!SC_SCB_IS_VALID(pstSCBPublish))
    {
        DOS_ASSERT(0);
        return DOS_FAIL;
    }

    pstSCBNotify = sc_scb_get(pstSCBPublish->usOtherSCBNo);
    if (!SC_SCB_IS_VALID(pstSCBNotify))
    {
        DOS_ASSERT(0);
        return DOS_FAIL;
    }

    pstSCBSubscription = sc_scb_get(pstSCBNotify->usOtherSCBNo);
    if (!SC_SCB_IS_VALID(pstSCBSubscription))
    {
        DOS_ASSERT(0);
        return DOS_FAIL;
    }

    if (SC_SCB_ACTIVE == pstSCBNotify->ucStatus)
    {
        if (SC_SCB_ACTIVE == pstSCBSubscription->ucStatus)
        {
            /* 接通订阅放和发起方 */
            if (pstSCBSubscription->ulLastHoldTimetamp)
            {
                dos_snprintf(szBuffCMD, sizeof(szBuffCMD), "uuid_hold off %s \r\n", pstSCBSubscription->szUUID);
                sc_ep_esl_execute_cmd(szBuffCMD);

                dos_snprintf(szBuffCMD, sizeof(szBuffCMD), "uuid_bridge %s %s \r\n", pstSCBNotify->szUUID, pstSCBSubscription->szUUID);
                sc_ep_esl_execute_cmd(szBuffCMD);
            }
            else
            {
                /* 没有断开过，发布方没有接听 */
            }
        }
        else
        {
            /* 要求挂断发起方 */
            sc_ep_esl_execute("hangup", "", pstSCBNotify->szUUID);
        }
    }
    else /* Release */
    {
        if (SC_SCB_ACTIVE == pstSCBSubscription->ucStatus)
        {
            /* 要求挂断订阅方 */
            sc_ep_esl_execute("hangup", "", pstSCBSubscription->szUUID);
        }
        else
        {
            /* 发送订阅方和发起方的话单 */
            /* 发送发布方的话单 */
            sc_send_billing_stop2bs(pstSCBSubscription);

            /* 清理资源 */
            sc_scb_free(pstSCBSubscription);
            /* 释放资源 */
        }
    }

    /* 发送transfer话单，使后续呼叫与transfer无关 */
    pstSCBPublish->usOtherSCBNo = U16_BUTT;
    pstSCBPublish->ucTranforRole = SC_TRANS_ROLE_BUTT;

    pstSCBNotify->usPublishSCB = U16_BUTT;
    pstSCBNotify->ucTranforRole = SC_TRANS_ROLE_BUTT;

    pstSCBSubscription->ucTranforRole = SC_TRANS_ROLE_BUTT;

    /* 发送发布方的话单 */
    sc_send_billing_stop2bs(pstSCBPublish);

    /* 清理资源 */
    sc_scb_free(pstSCBPublish);

    return DOS_SUCC;
}

U32 sc_ep_transfer_publish_active(SC_SCB_ST * pstSCBPublish)
{
    SC_SCB_ST* pstSCBSubscription = NULL;
    SC_SCB_ST* pstSCBNotify = NULL;
    S8         szBuffCMD[512] = { 0 };

    if (!SC_SCB_IS_VALID(pstSCBPublish))
    {
        DOS_ASSERT(0);

        return DOS_FAIL;
    }

    pstSCBNotify = sc_scb_get(pstSCBPublish->usOtherSCBNo);
    if (!SC_SCB_IS_VALID(pstSCBNotify))
    {
        DOS_ASSERT(0);

        return DOS_FAIL;
    }

    pstSCBSubscription = sc_scb_get(pstSCBNotify->usOtherSCBNo);
    if (!SC_SCB_IS_VALID(pstSCBSubscription))
    {
        DOS_ASSERT(0);

        return DOS_FAIL;
    }

    if (SC_SCB_ACTIVE == pstSCBNotify->ucStatus)
    {
        if (SC_SCB_ACTIVE == pstSCBSubscription->ucStatus)
        {
            /* hold 订阅放 */
            dos_snprintf(szBuffCMD, sizeof(szBuffCMD), "uuid_hold %s \r\n", pstSCBSubscription->szUUID);
            sc_ep_esl_execute_cmd(szBuffCMD);

            /* 接通订阅放和发起方 */
            dos_snprintf(szBuffCMD, sizeof(szBuffCMD), "uuid_bridge %s %s \r\n", pstSCBNotify->szUUID, pstSCBPublish->szUUID);
            sc_ep_esl_execute_cmd(szBuffCMD);
        }
        else
        {
            /* 接通发起方和发布方 */
            dos_snprintf(szBuffCMD, sizeof(szBuffCMD), "uuid_bridge %s %s \r\n", pstSCBNotify->szUUID, pstSCBPublish->szUUID);
            sc_ep_esl_execute_cmd(szBuffCMD);

            /* 维护各种状态，使后续呼叫与transfer流程无关 */
            pstSCBPublish->usOtherSCBNo = pstSCBNotify->usSCBNo;
            pstSCBNotify->usOtherSCBNo = pstSCBPublish->usSCBNo;
            pstSCBPublish->ucTranforRole = SC_TRANS_ROLE_BUTT;
            pstSCBSubscription->ucTranforRole = SC_TRANS_ROLE_BUTT;
            pstSCBNotify->ucTranforRole = SC_TRANS_ROLE_BUTT;

            /* 发送话单 */
            pstSCBSubscription->usOtherSCBNo = U16_BUTT;
            sc_send_billing_stop2bs(pstSCBSubscription);

            /* 清理资源 */
            sc_scb_free(pstSCBSubscription);
        }
    }
    else /* Release */
    {
        if (SC_SCB_ACTIVE == pstSCBSubscription->ucStatus)
        {
            if (sc_call_check_service(pstSCBPublish, SC_SERV_BLIND_TRANSFER))
            {
                /* 接通订阅方和发布方 */
                dos_snprintf(szBuffCMD, sizeof(szBuffCMD), "uuid_bridge %s %s \r\n", pstSCBSubscription->szUUID, pstSCBPublish->szUUID);
                sc_ep_esl_execute_cmd(szBuffCMD);

                /* 维护各种状态，使后续呼叫与transfer流程无关 */
                pstSCBPublish->usOtherSCBNo = pstSCBSubscription->usSCBNo;
                pstSCBSubscription->usOtherSCBNo = pstSCBPublish->usSCBNo;
                pstSCBPublish->ucTranforRole = SC_TRANS_ROLE_BUTT;
                pstSCBSubscription->ucTranforRole = SC_TRANS_ROLE_BUTT;
                pstSCBNotify->ucTranforRole = SC_TRANS_ROLE_BUTT;

                /* 发送话单 */
                pstSCBNotify->usOtherSCBNo = U16_BUTT;
                sc_send_billing_stop2bs(pstSCBNotify);

                /* 清理资源 */
                sc_scb_free(pstSCBNotify);

            }
            else if (sc_call_check_service(pstSCBPublish, SC_SERV_ATTEND_TRANSFER))
            {
                /* 要求发布方挂断，要求订阅方挂断 */
                sc_ep_esl_execute("hangup", "", pstSCBPublish->szUUID);
                sc_ep_esl_execute("hangup", "", pstSCBSubscription->szUUID);
            }
            else
            {
                DOS_ASSERT(0);
            }
        }
        else
        {
            /* 要求释发布方 */
            sc_ep_esl_execute("hangup", "", pstSCBPublish->szUUID);
        }
    }

    return DOS_SUCC;
}


U32 sc_ep_transfer_subscription_release(SC_SCB_ST * pstSCBSubscription)
{
    SC_SCB_ST* pstSCBNotify = NULL;
    SC_SCB_ST* pstSCBPublish = NULL;

    if (!SC_SCB_IS_VALID(pstSCBSubscription))
    {
        DOS_ASSERT(0);
        return DOS_FAIL;
    }

    pstSCBNotify = sc_scb_get(pstSCBSubscription->usOtherSCBNo);
    if (!SC_SCB_IS_VALID(pstSCBNotify))
    {
        DOS_ASSERT(0);
        return DOS_FAIL;
    }

    pstSCBPublish = sc_scb_get(pstSCBNotify->usPublishSCB);
    if (!SC_SCB_IS_VALID(pstSCBPublish))
    {
        DOS_ASSERT(0);
        return DOS_FAIL;
    }

    if (pstSCBPublish->ucStatus < SC_SCB_ACTIVE)
    {
        if (SC_SCB_ACTIVE == pstSCBNotify->ucStatus)
        {
            /* 置当前为release就好，等待发布方接通做处理 */
            SC_SCB_SET_SERVICE(pstSCBSubscription, SC_SCB_RELEASE);
        }
        else /* Release */
        {
            /* 要求发布方挂断(无法做到哦，这个时候发布方还没有接通，那就只有等到发布方接通的时候处理) */
        }
    }
    else if (SC_SCB_ACTIVE == pstSCBPublish->ucStatus)
    {
        if (SC_SCB_ACTIVE == pstSCBNotify->ucStatus)
        {
            /* 维护各种状态，使后续呼叫与transfer流程无关 */
            pstSCBPublish->usOtherSCBNo = pstSCBNotify->usSCBNo;
            pstSCBNotify->usOtherSCBNo = pstSCBPublish->usSCBNo;
            pstSCBPublish->ucTranforRole = SC_TRANS_ROLE_BUTT;
            pstSCBSubscription->ucTranforRole = SC_TRANS_ROLE_BUTT;
            pstSCBNotify->ucTranforRole = SC_TRANS_ROLE_BUTT;

            /* 发送话单 */
            pstSCBSubscription->usOtherSCBNo = U16_BUTT;
            sc_send_billing_stop2bs(pstSCBSubscription);

            /* 清理资源 */
            sc_scb_free(pstSCBSubscription);
        }
        else /* Release */
        {
            /* 要求发布方挂断 */
            sc_ep_esl_execute("hangup", "", pstSCBPublish->szUUID);
        }
    }
    else /* Release */
    {
        /* 这个流程有问题，应该在发布方挂断的时候进行处理 */
    }

    return DOS_SUCC;
}

U32 sc_ep_call_intercept(SC_SCB_ST * pstSCB)
{
    SC_SCB_ST *pstSCBAgent = NULL;
    SC_ACD_AGENT_INFO_ST stAgentInfo;

    /* 查找坐席 */
    if (sc_acd_get_agent_by_id(&stAgentInfo, pstSCB->ulAgentID) != DOS_SUCC)
    {
        DOS_ASSERT(0);

        sc_logr_info(SC_ESL, "Cannot intercept call for agent with id %u. Agent not found..", pstSCB->ulAgentID);
        goto proc_fail;
    }

    if (stAgentInfo.usSCBNo >= SC_MAX_SCB_NUM)
    {
        DOS_ASSERT(0);

        sc_logr_info(SC_ESL, "Cannot intercept call for agent with id %u. Agent handle a invalid SCB No(%u).", pstSCB->ulAgentID, stAgentInfo.usSCBNo);
        goto proc_fail;
    }

    pstSCBAgent = sc_scb_get(stAgentInfo.usSCBNo);
    if (DOS_ADDR_INVALID(pstSCBAgent) || !pstSCBAgent->bValid)
    {
        DOS_ASSERT(0);

        sc_logr_info(SC_ESL, "Cannot intercept call for agent with id %u. Agent handle a SCB(%u) is invalid.", pstSCB->ulAgentID, stAgentInfo.usSCBNo);
        goto proc_fail;
    }


    if (stAgentInfo.bIsTCBNoOther)
    {
        if (pstSCBAgent->usOtherSCBNo >= SC_MAX_SCB_NUM)
        {
            DOS_ASSERT(0);

            sc_logr_info(SC_ESL, "Cannot intercept call for agent with id %u. Agent handle a SCB(%u) is invalid.", pstSCBAgent->ulAgentID, pstSCBAgent->usOtherSCBNo);
            goto proc_fail;
        }

        pstSCBAgent = sc_scb_get(pstSCBAgent->usOtherSCBNo);
        if (DOS_ADDR_INVALID(pstSCBAgent) || !pstSCBAgent->bValid)
        {
            DOS_ASSERT(0);

            sc_logr_info(SC_ESL, "Cannot intercept call for agent with id %u. Agent handle a SCB(%u) is invalid.", pstSCBAgent->ulAgentID, pstSCBAgent->usOtherSCBNo);
            goto proc_fail;
        }
    }

    if ('\0' == pstSCBAgent->szUUID[0])
    {
        DOS_ASSERT(0);

        sc_logr_info(SC_ESL, "Cannot hangup call for agent with id %u. Agent handle a SCB(%u) without UUID.", pstSCB->ulAgentID, stAgentInfo.usSCBNo);
        goto proc_fail;
    }


    if (sc_call_check_service(pstSCB, SC_SERV_CALL_INTERCEPT))
    {
        sc_ep_esl_execute("eavesdrop", pstSCBAgent->szUUID, pstSCB->szUUID);
    }
    else if (sc_call_check_service(pstSCB, SC_SERV_CALL_WHISPERS))
    {
        sc_ep_esl_execute("queue_dtmf", "w2@500", pstSCB->szUUID);
        sc_ep_esl_execute("eavesdrop", pstSCBAgent->szUUID, pstSCB->szUUID);
    }
    else
    {
        DOS_ASSERT(0);
        goto proc_fail;
    }

    return DOS_SUCC;

proc_fail:

    sc_ep_terminate_call(pstSCB);

    return DOS_FAIL;
}

U32 sc_ep_call_transfer(SC_SCB_ST * pstSCB, S8 *pszCallee, BOOL bIsAttend)
{
    SC_SCB_ST * pstSCBNew = NULL;
    SC_SCB_ST * pstSCB1 = NULL;
    U32         ulServType;

    if (!SC_SCB_IS_VALID(pstSCB))
    {
        DOS_ASSERT(0);
        return DOS_FAIL;
    }

    pstSCB1 = sc_scb_get(pstSCB->usOtherSCBNo);
    if (!SC_SCB_IS_VALID(pstSCB1))
    {
        DOS_ASSERT(0);
        return DOS_FAIL;
    }

    if (DOS_ADDR_INVALID(pszCallee) || '\0' == pszCallee)
    {
        DOS_ASSERT(0);
        return DOS_FAIL;
    }

    ulServType = bIsAttend ? SC_SERV_ATTEND_TRANSFER : SC_SERV_BLIND_TRANSFER;

    /* 先呼叫callee, 如果有任务ID就使用任务所指定的主叫号码，如果没有就用客户的主叫号码 */
    pstSCBNew = sc_scb_alloc();
    if (DOS_ADDR_INVALID(pstSCBNew))
    {
        sc_logr_warning(SC_ESL, "%s", "Cannot make call for the API CMD. Alloc SCB FAIL.");
    }

    pstSCB->usPublishSCB = pstSCBNew->usSCBNo;
    pstSCB->ucTranforRole = SC_TRANS_ROLE_NOTIFY;
    sc_call_trace(pstSCB, "Call trace prepare. Nofify Leg. %u", pstSCB->ucTranforRole);

    pstSCB1->ucTranforRole = SC_TRANS_ROLE_SUBSCRIPTION;
    sc_call_trace(pstSCB1, "Call trace prepare. Subscription Leg. %u", pstSCB1->ucTranforRole);

    pstSCBNew->ulCustomID = pstSCB->ulCustomID;
    pstSCBNew->ulAgentID = pstSCB->ulAgentID;
    pstSCBNew->ulTaskID = pstSCB->ulTaskID;
    pstSCBNew->usOtherSCBNo = pstSCB->usSCBNo;
    pstSCBNew->ucTranforRole = SC_TRANS_ROLE_PUBLISH;
    pstSCBNew->ucMainService = ulServType;
    sc_call_trace(pstSCBNew, "Call trace prepare. Publish Leg. %u", pstSCBNew->ucTranforRole);

    /* 需要指定主叫号码 */
    dos_strncpy(pstSCBNew->szCalleeNum, pszCallee, sizeof(pstSCBNew->szCalleeNum));
    pstSCBNew->szCalleeNum[sizeof(pstSCBNew->szCalleeNum) - 1] = '\0';
    dos_strncpy(pstSCBNew->szCallerNum, pstSCB->szCallerNum, sizeof(pstSCBNew->szCallerNum));
    pstSCBNew->szCallerNum[sizeof(pstSCBNew->szCallerNum) - 1] = '\0';

    SC_SCB_SET_SERVICE(pstSCBNew, ulServType);

    if (!sc_ep_black_list_check(pstSCBNew->ulCustomID, pszCallee))
    {
        DOS_ASSERT(0);

        sc_logr_info(SC_ESL, "Cannot make call. Callee in blocak list. (%s)", pszCallee);

        goto proc_fail;
    }

    if (pstSCBNew->ulCustomID == sc_ep_get_custom_by_sip_userid(pszCallee)
        || sc_ep_check_extension(pszCallee, pstSCBNew->ulCustomID))
    {
        SC_SCB_SET_SERVICE(pstSCBNew, SC_SERV_OUTBOUND_CALL);
        SC_SCB_SET_SERVICE(pstSCBNew, SC_SERV_INTERNAL_CALL);
    }
    else
    {
        SC_SCB_SET_SERVICE(pstSCBNew, SC_SERV_OUTBOUND_CALL);
        SC_SCB_SET_SERVICE(pstSCBNew, SC_SERV_EXTERNAL_CALL);
    }

    if (sc_send_usr_auth2bs(pstSCBNew) != DOS_SUCC)
    {
        DOS_ASSERT(0);

        sc_logr_info(SC_ESL, "Cannot make call. Send auth fail.", pszCallee);

        goto proc_fail;
    }

    return DOS_SUCC;

proc_fail:
    if (DOS_ADDR_INVALID(pstSCBNew))
    {
        sc_scb_free(pstSCBNew);
    }

    return DOS_FAIL;
}

U32 sc_ep_call_agent(SC_SCB_ST * pstSCB, SC_ACD_AGENT_INFO_ST *pstAgentInfo)
{
    S8            szAPPParam[512] = { 0 };
    U32           ulErrCode = BS_TERM_NONE;
    SC_SCB_ST     *pstSCBNew = NULL;

    if (DOS_ADDR_INVALID(pstSCB) || DOS_ADDR_INVALID(pstAgentInfo))
    {
        DOS_ASSERT(0);

        return DOS_FAIL;
    }

    /* 如果坐席长连了，就需要特殊处理 */
    if (pstAgentInfo->bConnected)
    {
        pstSCBNew = sc_scb_get(pstAgentInfo->usSCBNo);
        if (DOS_ADDR_INVALID(pstSCBNew))
        {
            DOS_ASSERT(0);
            goto proc_error;
        }

        if ('\0' == pstSCBNew->szUUID[0])
        {
            DOS_ASSERT(0);
            goto proc_error;
        }

        dos_snprintf(szAPPParam, sizeof(szAPPParam), "bgapi uuid_bridge %s %s \r\n", pstSCB->szUUID, pstSCBNew->szUUID);
        if (sc_ep_esl_execute_cmd(szAPPParam) != DOS_SUCC)
        {
            DOS_ASSERT(0);
            goto proc_error;
        }

        return DOS_SUCC;
    }

    pstSCBNew = sc_scb_alloc();
    if (DOS_ADDR_INVALID(pstSCBNew))
    {
        DOS_ASSERT(0);

        sc_logr_error(SC_ESL, "%s", "Allc SCB FAIL.");
        ulErrCode = BS_TERM_SCB_LEEK;
        goto proc_error;
    }

    //pthread_mutex_lock(&pstSCBNew->mutexSCBLock);

    pstSCB->usOtherSCBNo = pstSCBNew->usSCBNo;
    pstSCBNew->ulCustomID = pstSCB->ulCustomID;
    pstSCBNew->ulAgentID = pstAgentInfo->ulSiteID;
    pstSCBNew->usOtherSCBNo = pstSCB->usSCBNo;
    pstSCBNew->ucLegRole = SC_CALLEE;
    pstSCBNew->bRecord = pstAgentInfo->bRecord;
    pstSCBNew->bIsAgentCall = DOS_TRUE;
    dos_strncpy(pstSCBNew->szCallerNum, pstSCB->szCalleeNum, sizeof(pstSCBNew->szCallerNum));
    pstSCBNew->szCallerNum[sizeof(pstSCBNew->szCallerNum) - 1] = '\0';
    dos_strncpy(pstSCBNew->szANINum, pstSCB->szCallerNum, sizeof(pstSCBNew->szANINum));
    pstSCBNew->szANINum[sizeof(pstSCBNew->szANINum) - 1] = '\0';
    switch (pstAgentInfo->ucBindType)
    {
        case AGENT_BIND_SIP:
            dos_strncpy(pstSCBNew->szCalleeNum, pstAgentInfo->szUserID, sizeof(pstSCBNew->szCalleeNum));
            pstSCBNew->szCalleeNum[sizeof(pstSCBNew->szCalleeNum) - 1] = '\0';
            break;
        case AGENT_BIND_TELE:
            dos_strncpy(pstSCBNew->szCalleeNum, pstAgentInfo->szTelePhone, sizeof(pstSCBNew->szCalleeNum));
            pstSCBNew->szCalleeNum[sizeof(pstSCBNew->szCalleeNum) - 1] = '\0';
            break;
        case AGENT_BIND_MOBILE:
            dos_strncpy(pstSCBNew->szCalleeNum, pstAgentInfo->szMobile, sizeof(pstSCBNew->szCalleeNum));
            pstSCBNew->szCalleeNum[sizeof(pstSCBNew->szCalleeNum) - 1] = '\0';
            break;
        case AGENT_BIND_TT_NUMBER:
            dos_strncpy(pstSCBNew->szCalleeNum, pstAgentInfo->szTTNumber, sizeof(pstSCBNew->szCalleeNum));
            pstSCBNew->szCalleeNum[sizeof(pstSCBNew->szCalleeNum) - 1] = '\0';
            break;
    }

    dos_strncpy(pstSCBNew->szSiteNum, pstAgentInfo->szEmpNo, sizeof(pstSCBNew->szSiteNum));
    pstSCBNew->szSiteNum[sizeof(pstSCBNew->szSiteNum) - 1] = '\0';
    //pthread_mutex_unlock(&pstSCB->mutexSCBLock);
    SC_SCB_SET_SERVICE(pstSCBNew, SC_SERV_AGENT_CALLBACK);

    SC_SCB_SET_STATUS(pstSCBNew, SC_SCB_EXEC);

    if (AGENT_BIND_SIP != pstAgentInfo->ucBindType
        && AGENT_BIND_TT_NUMBER != pstAgentInfo->ucBindType)
    {
        SC_SCB_SET_SERVICE(pstSCBNew, SC_SERV_OUTBOUND_CALL);
        SC_SCB_SET_SERVICE(pstSCBNew, SC_SERV_EXTERNAL_CALL);

        if (!sc_ep_black_list_check(pstSCBNew->ulCustomID, pstSCBNew->szCalleeNum))
        {
            DOS_ASSERT(0);
            sc_logr_info(SC_ESL, "Cannot make call for number %s which is in black list.", pstSCBNew->szCalleeNum);
            goto proc_error;
        }

        if (sc_send_usr_auth2bs(pstSCBNew) != DOS_SUCC)
        {
            sc_logr_notice(SC_ESL, "Send auth msg FAIL. SCB No: %d", pstSCBNew->usSCBNo);
            ulErrCode = BS_TERM_COMM_FAIL;
            goto proc_error;
        }

        return DOS_SUCC;
    }

    SC_SCB_SET_SERVICE(pstSCBNew, SC_SERV_OUTBOUND_CALL);
    SC_SCB_SET_SERVICE(pstSCBNew, SC_SERV_INTERNAL_CALL);

    if (AGENT_BIND_SIP == pstAgentInfo->ucBindType)
    {
        if (sc_dial_make_call2ip(pstSCBNew, SC_SERV_AGENT_CALLBACK) != DOS_SUCC)
        {
            goto proc_error;
        }
    }
    else if (AGENT_BIND_SIP == pstAgentInfo->ucBindType)
    {
        if (sc_dial_make_call2eix(pstSCBNew, SC_SERV_AGENT_CALLBACK) != DOS_SUCC)
        {
            goto proc_error;
        }
    }
#if 0
    dos_snprintf(szAPPParam, sizeof(szAPPParam)
                    , "{scb_number=%u,other_leg_scb=%u,main_service=%d,origination_caller_id_number=%s,origination_caller_id_name=%s}user/%s"
                    , pstSCBNew->usSCBNo
                    , pstSCBNew->usOtherSCBNo
                    , SC_SERV_AGENT_CALLBACK
                    , pstSCB->szCalleeNum
                    , pstSCB->szCalleeNum
                    , pstAgentInfo->szUserID);


    if (sc_ep_esl_execute("bridge", szAPPParam, pstSCB->szUUID) != DOS_SUCC)
    {
        /* @TODO 用户体验优化 */
        ulErrCode = BS_TERM_COMM_FAIL;
        goto proc_error;
    }
    else
    {
        /* @TODO 优化  先放音，再打坐席，坐席接通之后再连接到坐席 */
        sc_acd_agent_update_status(pstAgentInfo->ulSiteID, SC_ACD_BUSY, pstSCBNew->usSCBNo);

        sc_ep_esl_execute("sleep", "1000", pstSCB->szUUID);
        sc_ep_esl_execute("speak", "flite|kal|Is to connect you with an agent, please wait.", pstSCB->szUUID);
    }

#endif

    if (sc_ep_call_notify(pstAgentInfo, pstSCBNew->szANINum))
    {
        DOS_ASSERT(0);
    }

    return DOS_SUCC;

proc_error:
    if (pstSCBNew)
    {
        DOS_ASSERT(0);
        sc_scb_free(pstSCBNew);
        pstSCBNew = NULL;
    }
    return DOS_FAIL;
}

/**
 * 函数: sc_ep_call_agent_by_id
 * 功能: 呼叫某一个特定的坐席
 * 参数:
 *      SC_SCB_ST *pstSCB       : 业务控制块
 *      U32 ulAgentID           : 坐席ID
 * 返回值: 成功返回DOS_SUCC,失败返回DOS_FAIL
 */
U32 sc_ep_call_agent_by_id(SC_SCB_ST * pstSCB, U32 ulAgentID)
{
    SC_ACD_AGENT_INFO_ST stAgentInfo;

    /* 查找坐席 */
    if (sc_acd_get_agent_by_id(&stAgentInfo, ulAgentID) != DOS_SUCC)
    {
        DOS_ASSERT(0);

        sc_logr_info(SC_ESL, "Cannot call agent with id %u. Agent not found.", ulAgentID);
        goto proc_fail;
    }

    /* 呼叫坐席 */
    if (sc_ep_call_agent(pstSCB, &stAgentInfo) != DOS_SUCC)
    {
        goto proc_fail;
    }

    return DOS_SUCC;

proc_fail:
    if (DOS_ADDR_VALID(pstSCB))
    {
        sc_ep_hangup_call(pstSCB, BS_TERM_NONE);
    }
    return DOS_FAIL;
}

/**
 * 函数: U32 sc_ep_call_agent_by_grpid(SC_SCB_ST *pstSCB, U32 ulAgentQueue)
 * 功能: 群呼任务之后接通坐席
 * 参数:
 *      SC_SCB_ST *pstSCB       : 业务控制块
 * 返回值: 成功返回DOS_SUCC,失败返回DOS_FAIL
 */
U32 sc_ep_call_agent_by_grpid(SC_SCB_ST *pstSCB, U32 ulTaskAgentQueueID)
{
    U32           ulErrCode = BS_TERM_NONE;
    SC_ACD_AGENT_INFO_ST stAgentInfo;

    if (DOS_ADDR_INVALID(pstSCB))
    {
        DOS_ASSERT(0);

        return DOS_FAIL;
    }

    pstSCB->bIsInQueue = DOS_FALSE;

    /* 1.获取坐席队列，2.查找坐席。3.接通坐席 */
    if (U32_BUTT == ulTaskAgentQueueID)
    {
        DOS_ASSERT(0);

        sc_logr_info(SC_ESL, "Cannot get the agent queue for the task %d", pstSCB->ulTaskID);
        ulErrCode = BS_TERM_QUEUE_INVALID;
        goto proc_fail;
    }

    if (sc_acd_get_agent_by_grpid(&stAgentInfo, ulTaskAgentQueueID, pstSCB->szCalleeNum, pstSCB->usTCBNo) != DOS_SUCC)
    {
        DOS_ASSERT(0);

        sc_logr_notice(SC_ESL, "There is no useable agent for the task %d. Queue: %d. ", pstSCB->ulTaskID, ulTaskAgentQueueID);
        ulErrCode = BS_TERM_QUEUE_INVALID;
        goto proc_fail;
    }

    sc_logr_info(SC_ESL, "Select agent for call OK. Agent ID: %d, User ID: %s, Externsion: %s, Job-Num: %s"
                    , stAgentInfo.ulSiteID
                    , stAgentInfo.szUserID
                    , stAgentInfo.szExtension
                    , stAgentInfo.szEmpNo);


    /* 呼叫坐席 */
    if (sc_ep_call_agent(pstSCB, &stAgentInfo) != DOS_SUCC)
    {
        goto proc_fail;
    }

    return DOS_SUCC;

proc_fail:
    if (DOS_ADDR_VALID(pstSCB))
    {
        sc_ep_hangup_call(pstSCB, ulErrCode);
    }
    return DOS_FAIL;
}

/**
 * 函数: sc_ep_call_queue_add
 * 功能: 接通坐席时，将呼叫加入队列
 * 参数:
 *      SC_SCB_ST *pstSCB       : 业务控制块
 * 返回值: 成功返回DOS_SUCC,失败返回DOS_FAIL
 */
U32 sc_ep_call_queue_add(SC_SCB_ST *pstSCB, U32 ulTaskAgentQueueID)
{
    U32 ulResult;

    ulResult = sc_cwq_add_call(pstSCB, ulTaskAgentQueueID);
    if (ulResult == DOS_SUCC)
    {
        pstSCB->bIsInQueue = DOS_TRUE;
    }

    return ulResult;
}


U32 sc_ep_call_ctrl_proc(U32 ulAction, U32 ulTaskID, U32 ulAgent, U32 ulCustomerID, S8 *pszCallee)
{
    SC_SCB_ST *pstSCB       = NULL;
    SC_SCB_ST *pstOtherSCB  = NULL;
    SC_SCB_ST *pstSCBNew    = NULL;
    SC_ACD_AGENT_INFO_ST stAgentInfo;
    U32       ulMainServie;

    if (ulAction >= SC_API_CALLCTRL_BUTT)
    {
        DOS_ASSERT(0);

        goto proc_fail;
    }

    sc_logr_info(SC_ESL, "Start process call ctrl msg. Action: %u, Agent: %u, Customer: %u, Task: %u, Caller: %s"
                    , ulAction, ulAgent, ulCustomerID, ulTaskID, pszCallee);

    switch (ulAction)
    {
        case SC_API_MAKE_CALL:
            /* 查找坐席 */
            if (sc_acd_get_agent_by_id(&stAgentInfo, ulAgent) != DOS_SUCC)
            {
                DOS_ASSERT(0);

                sc_logr_info(SC_ESL, "Cannot call agent with id %u. Agent not found.", ulAgent);
                goto make_all_fail;
            }

            if (stAgentInfo.bConnected)
            {
                /* TODO 坐席长连 */

            }

            pstSCB = sc_scb_alloc();
            if (DOS_ADDR_INVALID(pstSCB))
            {
                sc_logr_warning(SC_ESL, "%s", "Cannot make call for the API CMD. Alloc SCB FAIL.");
            }

            pstSCB->ulCustomID = ulCustomerID;
            pstSCB->ulAgentID = ulAgent;
            pstSCB->ulTaskID = ulTaskID;

            /* 指定主叫号码 */
            dos_strncpy(pstSCB->szCallerNum, pszCallee, sizeof(pstSCB->szCallerNum));
            pstSCB->szCallerNum[sizeof(pstSCB->szCallerNum) - 1] = '\0';

            /* 指定被叫号码 */
            switch (stAgentInfo.ucBindType)
            {
                case AGENT_BIND_SIP:
                    dos_strncpy(pstSCB->szCalleeNum, stAgentInfo.szUserID, sizeof(pstSCB->szCalleeNum));
                    pstSCB->szCalleeNum[sizeof(pstSCB->szCalleeNum) - 1] = '\0';
                    break;
                case AGENT_BIND_TELE:
                    dos_strncpy(pstSCB->szCalleeNum, stAgentInfo.szTelePhone, sizeof(pstSCB->szCalleeNum));
                    pstSCB->szCalleeNum[sizeof(pstSCB->szCalleeNum) - 1] = '\0';
                    break;
                case AGENT_BIND_MOBILE:
                    dos_strncpy(pstSCB->szCalleeNum, stAgentInfo.szMobile, sizeof(pstSCB->szCalleeNum));
                    pstSCB->szCalleeNum[sizeof(pstSCB->szCalleeNum) - 1] = '\0';
                    break;
            }

            SC_SCB_SET_SERVICE(pstSCB, SC_SERV_PREVIEW_DIALING);

            SC_SCB_SET_SERVICE(pstSCB, SC_SERV_OUTBOUND_CALL);
            SC_SCB_SET_SERVICE(pstSCB, SC_SERV_INTERNAL_CALL);

            /* 呼叫坐席 */
            if (sc_dial_make_call2ip(pstSCB, SC_SERV_PREVIEW_DIALING) != DOS_SUCC)
            {
                DOS_ASSERT(0);

                sc_logr_info(SC_ESL, "Cannot make call. Make call to other endpoint fail. (%s)", pszCallee);
                goto make_all_fail;
            }

            break;
make_all_fail:
            if (DOS_ADDR_INVALID(pstSCB))
            {
                sc_scb_free(pstSCB);
                pstSCB = NULL;
            }

            goto proc_fail;
            break;

        case SC_API_HANGUP_CALL:
            /* 查找坐席 */
            if (sc_acd_get_agent_by_id(&stAgentInfo, ulAgent) != DOS_SUCC)
            {
                DOS_ASSERT(0);

                sc_logr_info(SC_ESL, "Cannot hangup call for agent with id %u. Agent not found..", ulAgent);
                goto proc_fail;
            }

            if (stAgentInfo.usSCBNo >= SC_MAX_SCB_NUM)
            {
                DOS_ASSERT(0);

                sc_logr_info(SC_ESL, "Cannot hangup call for agent with id %u. Agent handle a invalid SCB No(%u).", ulAgent, stAgentInfo.usSCBNo);
                goto proc_fail;
            }

            pstSCB = sc_scb_get(stAgentInfo.usSCBNo);
            if (DOS_ADDR_INVALID(pstSCB) || !pstSCB->bValid)
            {
                DOS_ASSERT(0);

                sc_logr_info(SC_ESL, "Cannot hangup call for agent with id %u. Agent handle a SCB(%u) is invalid.", ulAgent, stAgentInfo.usSCBNo);
                goto proc_fail;
            }

            if (stAgentInfo.bIsTCBNoOther)
            {
                if (pstSCB->usOtherSCBNo >= SC_MAX_SCB_NUM)
                {
                    DOS_ASSERT(0);

                    sc_logr_info(SC_ESL, "Cannot hangup call for agent with id %u. Agent handle a invalid SCB No(%u).", ulAgent, pstSCB->usOtherSCBNo);
                    goto proc_fail;
                }

                pstOtherSCB = sc_scb_get(pstSCB->usOtherSCBNo);
                if (DOS_ADDR_INVALID(pstOtherSCB) || !pstOtherSCB->bValid)
                {
                    DOS_ASSERT(0);

                    sc_logr_info(SC_ESL, "Cannot hangup call for agent with id %u. Agent handle a SCB(%u) is invalid.", ulAgent, pstSCB->usOtherSCBNo);
                    goto proc_fail;
                }

                if ('\0' == pstOtherSCB->szUUID[0])
                {
                    DOS_ASSERT(0);

                    sc_logr_info(SC_ESL, "Cannot hangup call for agent with id %u. Agent handle a SCB(%u) without UUID.", ulAgent, pstSCB->usOtherSCBNo);
                    goto proc_fail;
                }

                sc_ep_hangup_call(pstOtherSCB, BS_TERM_HANGUP);
            }
            else
            {
                if ('\0' == pstSCB->szUUID[0])
                {
                    DOS_ASSERT(0);

                    sc_logr_info(SC_ESL, "Cannot hangup call for agent with id %u. Agent handle a SCB(%u) without UUID.", ulAgent, stAgentInfo.usSCBNo);
                    goto proc_fail;
                }

                sc_ep_hangup_call(pstSCB, BS_TERM_HANGUP);
            }

            break;

        case SC_API_RECORD:
            /* 查找坐席 */
            if (sc_acd_get_agent_by_id(&stAgentInfo, ulAgent) != DOS_SUCC)
            {
                DOS_ASSERT(0);

                sc_logr_info(SC_ESL, "Cannot hangup call for agent with id %u. Agent not found..", ulAgent);
                goto proc_fail;
            }

            if (stAgentInfo.usSCBNo >= SC_MAX_SCB_NUM)
            {
                DOS_ASSERT(0);

                sc_logr_info(SC_ESL, "Cannot hangup call for agent with id %u. Agent handle a invalid SCB No(%u).", ulAgent, stAgentInfo.usSCBNo);
                goto proc_fail;
            }

            pstSCB = sc_scb_get(stAgentInfo.usSCBNo);
            if (DOS_ADDR_INVALID(pstSCB) || !pstSCB->bValid)
            {
                DOS_ASSERT(0);

                sc_logr_info(SC_ESL, "Cannot hangup call for agent with id %u. Agent handle a SCB(%u) is invalid.", ulAgent, stAgentInfo.usSCBNo);
                goto proc_fail;
            }

            if ('\0' == pstSCB->szUUID[0])
            {
                DOS_ASSERT(0);

                sc_logr_info(SC_ESL, "Cannot hangup call for agent with id %u. Agent handle a SCB(%u) without UUID.", ulAgent, stAgentInfo.usSCBNo);
                goto proc_fail;
            }

            if (sc_ep_agent_record(pstSCB) != DOS_SUCC)
            {
                goto proc_fail;
            }
            break;

        case SC_API_WHISPERS:
        case SC_API_INTERCEPT:
            /* 查找坐席 */
            if (sc_acd_get_agent_by_id(&stAgentInfo, ulAgent) != DOS_SUCC)
            {
                DOS_ASSERT(0);

                sc_logr_info(SC_ESL, "Cannot hangup call for agent with id %u. Agent not found..", ulAgent);
                goto proc_fail;
            }

            if (stAgentInfo.usSCBNo >= SC_MAX_SCB_NUM)
            {
                DOS_ASSERT(0);

                sc_logr_info(SC_ESL, "Cannot hangup call for agent with id %u. Agent handle a invalid SCB No(%u).", ulAgent, stAgentInfo.usSCBNo);
                goto proc_fail;
            }

            pstSCB = sc_scb_get(stAgentInfo.usSCBNo);
            if (DOS_ADDR_INVALID(pstSCB) || !pstSCB->bValid)
            {
                DOS_ASSERT(0);

                sc_logr_info(SC_ESL, "Cannot hangup call for agent with id %u. Agent handle a SCB(%u) is invalid.", ulAgent, stAgentInfo.usSCBNo);
                goto proc_fail;
            }

            if ('\0' == pstSCB->szUUID[0])
            {
                DOS_ASSERT(0);

                sc_logr_info(SC_ESL, "Cannot hangup call for agent with id %u. Agent handle a SCB(%u) without UUID.", ulAgent, stAgentInfo.usSCBNo);
                goto proc_fail;
            }

            pstSCBNew = sc_scb_alloc();
            if (DOS_ADDR_INVALID(pstSCBNew))
            {
                sc_logr_warning(SC_ESL, "%s", "Cannot make call for the API CMD. Alloc SCB FAIL..");
            }

            pstSCBNew->ulCustomID = ulCustomerID;
            pstSCBNew->ulAgentID = ulAgent;
            pstSCBNew->ulTaskID = ulTaskID;

            /* 需要指定主叫号码 */
            dos_strncpy(pstSCBNew->szCalleeNum, pszCallee, sizeof(pstSCBNew->szCalleeNum));
            pstSCBNew->szCalleeNum[sizeof(pstSCBNew->szCalleeNum) - 1] = '\0';
            dos_strncpy(pstSCBNew->szCallerNum, pszCallee, sizeof(pstSCBNew->szCallerNum));
            pstSCBNew->szCallerNum[sizeof(pstSCBNew->szCallerNum) - 1] = '\0';

            if (SC_API_WHISPERS == ulAction)
            {
                ulMainServie = SC_SERV_CALL_WHISPERS;
            }
            else
            {
                ulMainServie = SC_SERV_CALL_INTERCEPT;
            }

            SC_SCB_SET_SERVICE(pstSCBNew, ulMainServie);

            if (!sc_ep_black_list_check(ulCustomerID, pszCallee))
            {
                DOS_ASSERT(0);

                sc_logr_info(SC_ESL, "Cannot make call. Callee in blocak list. (%s)", pszCallee);

                goto make_all_fail1;
            }

            if (ulCustomerID == sc_ep_get_custom_by_sip_userid(pszCallee)
                || sc_ep_check_extension(pszCallee, ulCustomerID))
            {
                SC_SCB_SET_SERVICE(pstSCBNew, SC_SERV_OUTBOUND_CALL);
                SC_SCB_SET_SERVICE(pstSCBNew, SC_SERV_INTERNAL_CALL);

                if (sc_dial_make_call2ip(pstSCBNew, ulMainServie) != DOS_SUCC)
                {
                    DOS_ASSERT(0);

                    sc_logr_info(SC_ESL, "Cannot make call. Make call to other endpoint fail. (%s)", pszCallee);
                    goto make_all_fail1;
                }
            }
            else
            {
                SC_SCB_SET_SERVICE(pstSCBNew, SC_SERV_OUTBOUND_CALL);
                SC_SCB_SET_SERVICE(pstSCBNew, SC_SERV_EXTERNAL_CALL);
                if (sc_send_usr_auth2bs(pstSCBNew) != DOS_SUCC)
                {
                    DOS_ASSERT(0);

                    sc_logr_info(SC_ESL, "Cannot make call. Send auth fail.", pszCallee);

                    goto make_all_fail1;
                }
            }

            break;
make_all_fail1:
            if (DOS_ADDR_INVALID(pstSCBNew))
            {
                sc_scb_free(pstSCBNew);
                pstSCBNew = NULL;
            }

            goto proc_fail;
            break;

        case SC_API_TRANSFOR_ATTAND:
        case SC_API_TRANSFOR_BLIND:
            /* 查找坐席 */
            if (sc_acd_get_agent_by_id(&stAgentInfo, ulAgent) != DOS_SUCC)
            {
                DOS_ASSERT(0);

                sc_logr_info(SC_ESL, "Cannot hangup call for agent with id %u. Agent not found..", ulAgent);
                goto proc_fail;
            }

            if (stAgentInfo.usSCBNo >= SC_MAX_SCB_NUM)
            {
                DOS_ASSERT(0);

                sc_logr_info(SC_ESL, "Cannot hangup call for agent with id %u. Agent handle a invalid SCB No(%u).", ulAgent, stAgentInfo.usSCBNo);
                goto proc_fail;
            }

            pstSCB = sc_scb_get(stAgentInfo.usSCBNo);
            if (DOS_ADDR_INVALID(pstSCB) || !pstSCB->bValid)
            {
                DOS_ASSERT(0);

                sc_logr_info(SC_ESL, "Cannot hangup call for agent with id %u. Agent handle a SCB(%u) is invalid.", ulAgent, stAgentInfo.usSCBNo);
                goto proc_fail;
            }

            if (sc_ep_call_transfer(pstSCB, pszCallee, (ulAction == SC_API_TRANSFOR_ATTAND)) != DOS_SUCC)
            {
                goto proc_fail;
            }

            break;

        case SC_API_CONFERENCE:
            break;

        default:
            goto proc_fail;
    }

    sc_logr_info(SC_ESL, "Finished to process call ctrl msg. Action: %u, Agent: %u, Customer: %u, Task: %u, Caller: %s"
                    , ulAction, ulAgent, ulCustomerID, ulTaskID, pszCallee);

    return DOS_SUCC;

proc_fail:
    sc_logr_info(SC_ESL, "Process call ctrl msg FAIL. Action: %u, Agent: %u, Customer: %u, Task: %u, Caller: %s"
                    , ulAction, ulAgent, ulCustomerID, ulTaskID, pszCallee);

    return DOS_FAIL;
}


/**
 * 函数: U32 sc_ep_incoming_call_proc(esl_handle_t *pstHandle, esl_event_t *pstEvent, SC_SCB_ST *pstSCB)
 * 功能: 处理由PSTN呼入到SIP测的呼叫
 * 参数:
 *      esl_event_t *pstEvent   : ESL 事件
 *      esl_handle_t *pstHandle : 发送数据的handle
 *      SC_SCB_ST *pstSCB       : 业务控制块
 * 返回值: 成功返回DOS_SUCC,失败返回DOS_FAIL
 */
U32 sc_ep_incoming_call_proc(SC_SCB_ST *pstSCB)
{
    U32   ulCustomerID = U32_BUTT;
    U32   ulBindType = U32_BUTT;
    U32   ulBindID = U32_BUTT;
    S8    szCallString[512] = { 0, };
    S8    szCallee[32] = { 0, };
    U32   ulErrCode = BS_TERM_NONE;

    if (DOS_ADDR_INVALID(pstSCB))
    {
        DOS_ASSERT(0);

        goto proc_fail;
    }

    ulCustomerID = sc_ep_get_custom_by_did(pstSCB->szCalleeNum);
    if (U32_BUTT != ulCustomerID)
    {
        pstSCB->ulCustomID = ulCustomerID;

        if (sc_ep_get_bind_info4did(pstSCB->szCalleeNum, &ulBindType, &ulBindID) != DOS_SUCC
            || ulBindType >=SC_DID_BIND_TYPE_BUTT
            || U32_BUTT == ulBindID)
        {
            DOS_ASSERT(0);

            sc_logr_info(SC_ESL, "Cannot get the bind info for the DID number %s, Reject Call.", pstSCB->szCalleeNum);
            ulErrCode = BS_TERM_NUM_INVALID;
            goto proc_fail;
        }

        sc_logr_info(SC_ESL, "Process Incoming Call, DID Number %s. Bind Type: %u, Bind ID: %u"
                        , pstSCB->szCalleeNum
                        , ulBindType
                        , ulBindID);

        switch (ulBindType)
        {
            case SC_DID_BIND_TYPE_SIP:
                if (DOS_SUCC != sc_ep_get_userid_by_id(ulBindID, szCallee, sizeof(szCallee)))
                {
                    DOS_ASSERT(0);

                    sc_logr_info(SC_ESL, "DID number %s seems donot bind a SIP User ID, Reject Call.", pstSCB->szCalleeNum);
                    ulErrCode = BS_TERM_INTERNAL_ERR;
                    goto proc_fail;
                }

                dos_snprintf(szCallString, sizeof(szCallString), "{other_leg_scb=%d,update_agent=%s}user/%s", pstSCB->usSCBNo, szCallee, szCallee);

                sc_ep_esl_execute("bridge", szCallString, pstSCB->szUUID);
                sc_ep_esl_execute("hangup", szCallString, pstSCB->szUUID);
                break;

            case SC_DID_BIND_TYPE_QUEUE:
                /* TODO 这种情况下的，将坐席的usSCBNo字段赋值的问题，需要修改 */
                if (sc_ep_call_queue_add(pstSCB, ulBindID) != DOS_SUCC)
                {
                    DOS_ASSERT(0);

                    sc_logr_info(SC_ESL, "Add Call to call waiting queue FAIL.Callee: %s. Reject Call.", pstSCB->szCalleeNum);
                    ulErrCode = BS_TERM_INTERNAL_ERR;
                    goto proc_fail;
                }
                break;

            default:
                DOS_ASSERT(0);

                sc_logr_info(SC_ESL, "DID number %s has bind an error number, Reject Call.", pstSCB->szCalleeNum);
                goto proc_fail;
        }
    }
    else
    {
        DOS_ASSERT(0);

        sc_logr_info(SC_ESL, "Destination is not a DID number, Reject Call. Destination: %s", pstSCB->szCalleeNum);
        goto proc_fail;

    }

    SC_SCB_SET_STATUS(pstSCB, SC_SCB_EXEC);

    return DOS_SUCC;

proc_fail:
    if (pstSCB)
    {
        sc_ep_hangup_call(pstSCB, ulErrCode);
    }

    return DOS_FAIL;
}

/**
 * 函数: U32 sc_ep_outgoing_call_proc(esl_handle_t *pstHandle, esl_event_t *pstEvent, SC_SCB_ST *pstSCB)
 * 功能: 处理由SIP呼入到PSTN侧的呼叫
 * 参数:
 *      esl_handle_t *pstHandle : 发送数据的handle
 *      esl_event_t *pstEvent   : ESL 事件
 *      SC_SCB_ST *pstSCB       : 业务控制块
 * 返回值: 成功返回DOS_SUCC,失败返回DOS_FAIL
 */
U32 sc_ep_outgoing_call_proc(SC_SCB_ST *pstSCB)
{
    SC_SCB_ST *pstSCBNew  = NULL;

    if (DOS_ADDR_INVALID(pstSCB))
    {
        DOS_ASSERT(0);

        goto proc_fail;
    }

    pstSCBNew = sc_scb_alloc();
    if (DOS_ADDR_INVALID(pstSCBNew))
    {
        DOS_ASSERT(0);

        sc_logr_error(SC_ESL, "Alloc SCB FAIL, %s:%d", __FUNCTION__, __LINE__);

        goto proc_fail;
    }

    pthread_mutex_lock(&pstSCBNew->mutexSCBLock);
    pstSCB->usOtherSCBNo = pstSCBNew->usSCBNo;
    pstSCBNew->ulCustomID = pstSCB->ulCustomID;
    pstSCBNew->usOtherSCBNo = pstSCB->usSCBNo;
    pstSCBNew->ucLegRole = SC_CALLEE;

    /* @todo 主叫号码应该使用客户的DID号码 */
    dos_strncpy(pstSCBNew->szCallerNum, pstSCB->szCallerNum, sizeof(pstSCBNew->szCallerNum));
    pstSCBNew->szCallerNum[sizeof(pstSCBNew->szCallerNum) - 1] = '\0';
    dos_strncpy(pstSCBNew->szCalleeNum, pstSCB->szCalleeNum, sizeof(pstSCBNew->szCalleeNum));
    pstSCBNew->szCalleeNum[sizeof(pstSCBNew->szCalleeNum) - 1] = '\0';
    dos_strncpy(pstSCBNew->szANINum, pstSCB->szCallerNum, sizeof(pstSCBNew->szANINum));
    pstSCBNew->szANINum[sizeof(pstSCBNew->szANINum) - 1] = '\0';

    pthread_mutex_unlock(&pstSCBNew->mutexSCBLock);
    SC_SCB_SET_SERVICE(pstSCBNew, SC_SERV_OUTBOUND_CALL);
    SC_SCB_SET_SERVICE(pstSCBNew, SC_SERV_EXTERNAL_CALL);
    SC_SCB_SET_STATUS(pstSCBNew, SC_SCB_INIT);

    if (!sc_ep_black_list_check(pstSCBNew->ulCustomID, pstSCBNew->szCalleeNum))
    {
        DOS_ASSERT(0);
        sc_logr_info(SC_ESL, "Cannot make call for number %s which is in black list.", pstSCBNew->szCalleeNum);
        goto proc_fail;
    }

    if (sc_send_usr_auth2bs(pstSCBNew) != DOS_SUCC)
    {
        sc_logr_notice(SC_ESL, "Send auth msg FAIL. SCB No: %d", pstSCBNew->usSCBNo);

        goto proc_fail;
    }

#if 0
    if (sc_ep_dst_is_black(pstSCB->szCalleeNum))
    {
        DOS_ASSERT(0);

        sc_call_trace(pstSCB,"The callee is in BLACK LIST. The call will be hungup later. UUID: %s", pstSCB->szUUID);
        goto proc_fail;
    }

    ulRouteID = sc_ep_search_route(pstSCB);
    if (U32_BUTT == ulRouteID)
    {
        DOS_ASSERT(0);

        sc_call_trace(pstSCB,"Find trunk gruop FAIL. The call will be hungup later. UUID: %s", pstSCB->szUUID);
        goto proc_fail;
    }
    sc_logr_info(SC_ESL, "Search Route SUCC. Route ID: %d", ulRouteID);

    pstSCB->ulTrunkID = ulRouteID;
    if (DOS_SUCC != sc_ep_get_callee_string(ulRouteID, pstSCB->szCalleeNum, szCallString, sizeof(szCallString)))
    {
        DOS_ASSERT(0);

        sc_call_trace(pstSCB,"Make call string FAIL. The call will be hungup later. UUID: %s", pstSCB->szUUID);
        goto proc_fail;
    }
    sc_logr_info(SC_ESL, "Make Call String SUCC. Call String: %s", szCallString);

    dos_snprintf(szCallParam, sizeof(szCallParam), "{other_leg_scb=%d}%s", pstSCB->usSCBNo, szCallString);

    /* 接听主叫方呼叫 */
    sc_ep_esl_execute("answer", NULL, pstSCB->szUUID);
    sc_ep_esl_execute("bridge", szCallParam, pstSCB->szUUID);
    sc_ep_esl_execute("hangup", "", pstSCB->szUUID);

    SC_SCB_SET_STATUS(pstSCB, SC_SCB_EXEC);
#endif
    return DOS_SUCC;

proc_fail:
    if (DOS_ADDR_VALID(pstSCB))
    {
        /* @TODO  优化。不要直接给挂断了 */
        sc_ep_esl_execute("hangup", "", pstSCB->szUUID);
    }

    if (DOS_ADDR_VALID(pstSCBNew))
    {
        DOS_ASSERT(0);
        sc_scb_free(pstSCBNew);
    }

    return DOS_FAIL;
}

/**
 * 函数: U32 sc_ep_auto_dial_proc(esl_handle_t *pstHandle, esl_event_t *pstEvent, SC_SCB_ST *pstSCB)
 * 功能: 处理由系统自动发起的呼叫
 * 参数:
 *      esl_handle_t *pstHandle : 发送数据的handle
 *      esl_event_t *pstEvent   : ESL 事件
 *      SC_SCB_ST *pstSCB       : 业务控制块
 * 返回值: 成功返回DOS_SUCC,失败返回DOS_FAIL
 */
U32 sc_ep_auto_dial_proc(esl_handle_t *pstHandle, esl_event_t *pstEvent, SC_SCB_ST *pstSCB)
{
    S8      szAPPParam[512]    = { 0, };
    U32     ulTaskMode         = U32_BUTT;
    U32     ulErrCode          = BS_TERM_NONE;

    SC_TRACE_IN(pstEvent, pstHandle, pstSCB, 0);

    if (DOS_ADDR_INVALID(pstEvent)
        || DOS_ADDR_INVALID(pstHandle)
        || DOS_ADDR_INVALID(pstSCB))
    {
        DOS_ASSERT(0);

        SC_TRACE_OUT();
        goto auto_call_proc_error;
    }

    sc_call_trace(pstSCB, "Start process event %s.", esl_event_get_header(pstEvent, "Event-Name"));

    if (!sc_call_check_service(pstSCB, SC_SERV_AUTO_DIALING))
    {
        DOS_ASSERT(0);

        sc_logr_debug(SC_ESL, "Process event %s finished. SCB do not include service auto call."
                            , esl_event_get_header(pstEvent, "Event-Name"));

        ulErrCode = BS_TERM_SERV_FAIL;
        goto auto_call_proc_error;
    }

    ulTaskMode = sc_task_get_mode(pstSCB->usTCBNo);
    if (ulTaskMode >= SC_TASK_MODE_BUTT)
    {
        DOS_ASSERT(0);

        sc_logr_debug(SC_ESL, "Process event %s finished. Cannot get the task mode for task %d."
                            , esl_event_get_header(pstEvent, "Event-Name")
                            , pstSCB->usTCBNo);

        ulErrCode = BS_TERM_TASK_PARAM_ERR;
        goto auto_call_proc_error;
    }

    /* 自动外呼需要处理 */
    /* 1.AOTO CALL走到这里客户那段已经接通了。这里根据所属任务的类型，做相应动作就好 */
    switch (ulTaskMode)
    {
        /* 需要放音的，统一先放音。在放音结束后请处理后续流程 */
        case SC_TASK_MODE_KEY4AGENT:
        case SC_TASK_MODE_KEY4AGENT1:
			sc_ep_esl_execute("set", "ignore_early_media=true", pstSCB->szUUID);
            sc_ep_esl_execute("set", "timer_name=soft", pstSCB->szUUID);
            sc_ep_esl_execute("sleep", "500", pstSCB->szUUID);

            dos_snprintf(szAPPParam, sizeof(szAPPParam)
                            , "1 1 %u 0 # %s pdtmf \\d+"
                            , sc_task_audio_playcnt(pstSCB->usTCBNo)
                            , sc_task_get_audio_file(pstSCB->usTCBNo));

            sc_ep_esl_execute("play_and_get_digits", szAPPParam, pstSCB->szUUID);
            pstSCB->ucCurrentPlyCnt = sc_task_audio_playcnt(pstSCB->usTCBNo);
            break;

        case SC_TASK_MODE_AUDIO_ONLY:
        case SC_TASK_MODE_AGENT_AFTER_AUDIO:
            sc_ep_esl_execute("set", "ignore_early_media=true", pstSCB->szUUID);
            sc_ep_esl_execute("set", "timer_name=soft", pstSCB->szUUID);
            sc_ep_esl_execute("sleep", "500", pstSCB->szUUID);

            dos_snprintf(szAPPParam, sizeof(szAPPParam)
                            , "+%d %s"
                            , sc_task_audio_playcnt(pstSCB->usTCBNo)
                            , sc_task_get_audio_file(pstSCB->usTCBNo));

            sc_ep_esl_execute("loop_playback", szAPPParam, pstSCB->szUUID);
            pstSCB->ucCurrentPlyCnt = sc_task_audio_playcnt(pstSCB->usTCBNo);

            break;

        /* 直接接通坐席 */
        case SC_TASK_MODE_DIRECT4AGETN:
            sc_ep_call_queue_add(pstSCB, sc_task_get_agent_queue(pstSCB->usTCBNo));

            break;

        default:
            DOS_ASSERT(0);
            ulErrCode = BS_TERM_TASK_PARAM_ERR;
            goto auto_call_proc_error;
    }

    SC_SCB_SET_STATUS(pstSCB, SC_SCB_ACTIVE);

    sc_call_trace(pstSCB, "Finished to process %s event.", esl_event_get_header(pstEvent, "Event-Name"));

    SC_TRACE_OUT();
    return DOS_SUCC;

auto_call_proc_error:
    sc_call_trace(pstSCB, "FAILED to process %s event.", esl_event_get_header(pstEvent, "Event-Name"));

    if (DOS_ADDR_VALID(pstSCB))
    {
        SC_SCB_SET_STATUS(pstSCB, SC_SCB_RELEASE);
        sc_ep_hangup_call(pstSCB, ulErrCode);
    }

    return DOS_FAIL;
}

/**
 * 函数: U32 sc_ep_num_verify(esl_handle_t *pstHandle, esl_event_t *pstEvent, SC_SCB_ST *pstSCB)
 * 功能: 执行语音验证码业务
 * 参数:
 *      esl_handle_t *pstHandle : 发送数据的handle
 *      esl_event_t *pstEvent   : ESL 事件
 *      SC_SCB_ST *pstSCB       : 业务控制块
 * 返回值: 成功返回DOS_SUCC,失败返回DOS_FAIL
 */
U32 sc_ep_num_verify(esl_handle_t *pstHandle, esl_event_t *pstEvent, SC_SCB_ST *pstSCB)
{
    S8 szCmdParam[128] = { 0 };
    U32 ulPlayCnt = 0;

    ulPlayCnt = pstSCB->ucCurrentPlyCnt;
    if (ulPlayCnt < SC_NUM_VERIFY_TIME_MIN
        || ulPlayCnt > SC_NUM_VERIFY_TIME_MAX)
    {
        ulPlayCnt = SC_NUM_VERIFY_TIME;
    }


    if (DOS_ADDR_INVALID(pstSCB))
    {
        DOS_ASSERT(0);

        return DOS_FAIL;
    }

    if (DOS_ADDR_INVALID(pstHandle)
        || DOS_ADDR_INVALID(pstEvent))
    {
        DOS_ASSERT(0);

        sc_ep_esl_execute("hangup", NULL, pstSCB->szUUID);

        return DOS_FAIL;
    }

    dos_snprintf(szCmdParam, sizeof(szCmdParam), "en name_spelled iterated %s", pstSCB->szDialNum);

    sc_ep_esl_execute("answer", NULL, pstSCB->szUUID);
    sc_ep_esl_execute("sleep", "1000", pstSCB->szUUID);

    while (ulPlayCnt-- > 0)
    {
        sc_ep_esl_execute("speak", "flite|kal|You verification code is: ", pstSCB->szUUID);
        sc_ep_esl_execute("say", szCmdParam, pstSCB->szUUID);
        sc_ep_esl_execute("sleep", "1000", pstSCB->szUUID);
    }

    sc_ep_esl_execute("hangup", NULL, pstSCB->szUUID);

    return DOS_SUCC;
}

/**
 * 函数: U32 sc_ep_internal_call_process(esl_handle_t *pstHandle, esl_event_t *pstEvent, SC_SCB_ST *pstSCB)
 * 功能: 处理内部呼叫
 * 参数:
 *      esl_handle_t *pstHandle : 发送数据的handle
 *      esl_event_t *pstEvent   : ESL 事件
 *      SC_SCB_ST *pstSCB       : 业务控制块
 * 返回值: 成功返回DOS_SUCC,失败返回DOS_FAIL
 */
U32 sc_ep_internal_call_process(esl_handle_t *pstHandle, esl_event_t *pstEvent, SC_SCB_ST *pstSCB)
{
    S8    *pszSrcNum = NULL;
    S8    *pszDstNum = NULL;
    S8    *pszUUID = NULL;
    S8    szSIPUserID[32] = { 0, };
    S8    szCallString[512] = { 0, };
    U32   ulCustomerID = U32_BUTT;
    U32   ulCustomerID1 = U32_BUTT;
    U32   ulErrCode = BS_TERM_NONE;

    if (DOS_ADDR_INVALID(pstEvent) || DOS_ADDR_INVALID(pstHandle) || DOS_ADDR_INVALID(pstSCB))
    {
        DOS_ASSERT(0);

        return DOS_FAIL;
    }

    /* 获取事件的UUID */
    pszUUID = esl_event_get_header(pstEvent, "Unique-ID");
    if (DOS_ADDR_INVALID(pszUUID))
    {
        DOS_ASSERT(0);

        SC_TRACE_OUT();
        ulErrCode = BS_TERM_UNKNOW;
        goto process_fail;
    }

    pszDstNum = esl_event_get_header(pstEvent, "Caller-Destination-Number");
    if (DOS_ADDR_INVALID(pszDstNum))
    {
        DOS_ASSERT(0);

        ulErrCode = BS_TERM_UNKNOW;
        goto process_fail;
    }

    pszDstNum = esl_event_get_header(pstEvent, "Caller-Destination-Number");
    pszSrcNum = esl_event_get_header(pstEvent, "Caller-Caller-ID-Number");
    if (DOS_ADDR_INVALID(pszDstNum)
        || DOS_ADDR_INVALID(pszSrcNum))
    {
        DOS_ASSERT(0);

        ulErrCode = BS_TERM_UNKNOW;
        goto process_fail;
    }

    /* 判断被叫号码是否是分机号，如果是分机号，就要找到对应的SIP账户，再呼叫，同时呼叫之前还需要获取主叫的分机号，修改ANI为主叫的分机号 */
    //ulCustomerID = sc_ep_get_custom_by_sip_userid(pszSrcNum);
    ulCustomerID = pstSCB->ulCustomID;
    if (U32_BUTT == ulCustomerID)
    {
        DOS_ASSERT(0);

        sc_logr_info(SC_ESL, "The source number %s seem not beyound to any customer, Reject Call", pszSrcNum);
        ulErrCode = BS_TERM_CUSTOM_INVALID;
        goto process_fail;
    }

    ulCustomerID1 = sc_ep_get_custom_by_sip_userid(pszDstNum);
    if (U32_BUTT != ulCustomerID1)
    {
        if (ulCustomerID == ulCustomerID1)
        {
            dos_snprintf(szCallString, sizeof(szCallString), "user/%s", pszDstNum);
            sc_ep_esl_execute("bridge", szCallString, pszUUID);
            //sc_ep_esl_execute("hangup", NULL, pszUUID);
        }
        else
        {
            DOS_ASSERT(0);

            sc_logr_info(SC_ESL, "Cannot call other customer direct, Reject Call. Src %s is owned by customer %d, Dst %s is owned by customer %d"
                            , pszSrcNum, ulCustomerID, pszDstNum, ulCustomerID1);
            ulErrCode = BS_TERM_SERV_FORBID;
            goto process_fail;
        }
    }
    else
    {
        if (sc_ep_get_userid_by_extension(ulCustomerID, pszDstNum, szSIPUserID, sizeof(szSIPUserID)) != DOS_SUCC)
        {
            DOS_ASSERT(0);

            sc_logr_info(SC_ESL, "Destination number %s is not seems a SIP User ID or Extension. Reject Call", pszDstNum);
            ulErrCode = BS_TERM_INTERNAL_ERR;
            goto process_fail;
        }

        dos_snprintf(szCallString, sizeof(szCallString), "user/%s", szSIPUserID);
        sc_ep_esl_execute("bridge", szCallString, pszUUID);
        //sc_ep_esl_execute("hangup", NULL, pszUUID);
    }

    return DOS_SUCC;

process_fail:
    if (pstSCB)
    {
        sc_ep_hangup_call(pstSCB, ulErrCode);
    }

    return DOS_FAIL;
}

/**
 * 函数: U32 sc_ep_internal_call_process(esl_handle_t *pstHandle, esl_event_t *pstEvent, SC_SCB_ST *pstSCB)
 * 功能: 处理内部呼叫
 * 参数:
 *      esl_handle_t *pstHandle : 发送数据的handle
 *      esl_event_t *pstEvent   : ESL 事件
 *      SC_SCB_ST *pstSCB       : 业务控制块
 * 返回值: 成功返回DOS_SUCC,失败返回DOS_FAIL
 */
U32 sc_ep_internal_service_proc(esl_handle_t *pstHandle, esl_event_t *pstEvent, SC_SCB_ST *pstSCB)
{
    S8    *pszUUID = NULL;

    sc_logr_info(SC_ESL, "%s", "Start exec internal service.");

    pszUUID = esl_event_get_header(pstEvent, "Unique-ID");
    if (DOS_ADDR_INVALID(pszUUID))
    {
        DOS_ASSERT(0);

        SC_TRACE_OUT();
        return DOS_FAIL;
    }

    sc_ep_esl_execute("answer", NULL, pszUUID);
    sc_ep_esl_execute("sleep", "1000", pszUUID);
    sc_ep_esl_execute("speak", "flite|kal|Temporary not support.", pszUUID);
    sc_ep_esl_execute("hangup", NULL, pszUUID);
    sc_ep_hangup_call(pstSCB, BS_TERM_SERV_INVALID);
    return DOS_SUCC;
}


/**
 * 函数: U32 sc_ep_system_stat(esl_event_t *pstEvent)
 * 功能: 统计信息
 * 参数:
 *      esl_event_t *pstEvent   : 事件
 * 返回值: 成功返回DOS_SUCC，否则返回DOS_FAIL
 */
U32 sc_ep_system_stat(esl_event_t *pstEvent)
{
    S8 *pszProfileName         = NULL;
    S8 *pszGatewayName         = NULL;
    S8 *pszOtherLeg            = NULL;
    S8 *pszSIPHangupCause      = NULL;
    U32 ulGatewayID            = 0;
    HASH_NODE_S   *pstHashNode = NULL;
    SC_GW_NODE_ST *pstGateway  = NULL;
    U32  ulIndex = U32_BUTT;

    if (DOS_ADDR_INVALID(pstEvent))
    {
        DOS_ASSERT(0);
        return DOS_FAIL;
    }


    pszGatewayName = esl_event_get_header(pstEvent, "variable_sip_profile_name");
    if (DOS_ADDR_VALID(pszGatewayName)
        && dos_strncmp(pszGatewayName, "gateway", dos_strlen("gateway")) == 0)
    {
        pszGatewayName = esl_event_get_header(pstEvent, "variable_sip_gateway_name");
        if (DOS_ADDR_VALID(pszGatewayName)
            && dos_atoul(pszGatewayName, &ulGatewayID) >= 0)
        {
            ulIndex = sc_ep_gw_hash_func(ulGatewayID);
            pthread_mutex_lock(&g_mutexHashGW);
            pstHashNode = hash_find_node(g_pstHashGW, ulIndex, (VOID *)&ulGatewayID, sc_ep_gw_hash_find);
            if (DOS_ADDR_VALID(pstHashNode)
                && DOS_ADDR_VALID(pstHashNode->pHandle))
            {
                pstGateway = pstHashNode->pHandle;
            }
            else
            {
                pstGateway = NULL;
                DOS_ASSERT(0);
            }
            pthread_mutex_unlock(&g_mutexHashGW);
        }
    }

    if (DOS_ADDR_INVALID(pstGateway))
    {
        DOS_ASSERT(0);
        return DOS_FAIL;
    }

    pthread_mutex_lock(&g_mutexHashGW);
    if (ESL_EVENT_CHANNEL_CREATE == pstEvent->event_id)
    {
        g_pstTaskMngtInfo->stStat.ulTotalSessions++;
        g_pstTaskMngtInfo->stStat.ulCurrentSessions++;
        if (g_pstTaskMngtInfo->stStat.ulCurrentSessions > g_pstTaskMngtInfo->stStat.ulMaxSession)
        {
            g_pstTaskMngtInfo->stStat.ulMaxSession = g_pstTaskMngtInfo->stStat.ulCurrentSessions;
        }

        if (pstGateway && DOS_FALSE != pstGateway->bStatus)
        {
            pstGateway->stStat.ulTotalSessions++;
            pstGateway->stStat.ulCurrentSessions++;
            if (pstGateway->stStat.ulCurrentSessions > pstGateway->stStat.ulMaxSession)
            {
                pstGateway->stStat.ulMaxSession = pstGateway->stStat.ulCurrentSessions;
            }
        }

        pszOtherLeg = esl_event_get_header(pstEvent, "variable_other_leg_scb");
        if (DOS_ADDR_INVALID(pszOtherLeg))
        {
            g_pstTaskMngtInfo->stStat.ulTotalCalls++;
            g_pstTaskMngtInfo->stStat.ulCurrentCalls++;
            if (g_pstTaskMngtInfo->stStat.ulCurrentCalls > g_pstTaskMngtInfo->stStat.ulMaxCalls)
            {
                g_pstTaskMngtInfo->stStat.ulMaxCalls = g_pstTaskMngtInfo->stStat.ulCurrentCalls;
            }

            if (pstGateway && DOS_FALSE != pstGateway->bStatus)
            {
                pstGateway->stStat.ulTotalCalls++;
                pstGateway->stStat.ulCurrentCalls++;
                if (pstGateway->stStat.ulCurrentCalls > pstGateway->stStat.ulMaxCalls)
                {
                    pstGateway->stStat.ulMaxCalls = pstGateway->stStat.ulCurrentCalls;
                }
            }
        }

        pszProfileName = esl_event_get_header(pstEvent, "variable_is_outbound");
        if (DOS_ADDR_VALID(pszProfileName)
            && dos_strncmp(pszProfileName, "true", dos_strlen("true")) == 0)
        {
            g_pstTaskMngtInfo->stStat.ulOutgoingSessions++;

            if (pstGateway && DOS_FALSE != pstGateway->bStatus)
            {
                pstGateway->stStat.ulOutgoingSessions++;
            }
        }
        else
        {
            g_pstTaskMngtInfo->stStat.ulIncomingSessions++;

            if (pstGateway && DOS_FALSE != pstGateway->bStatus)
            {
                pstGateway->stStat.ulIncomingSessions++;
            }
        }
    }
    else if (ESL_EVENT_CHANNEL_HANGUP_COMPLETE == pstEvent->event_id)
    {
        g_pstTaskMngtInfo->stStat.ulCurrentSessions--;

        if (pstGateway && DOS_FALSE != pstGateway->bStatus)
        {
            pstGateway->stStat.ulCurrentSessions--;
        }


        pszOtherLeg = esl_event_get_header(pstEvent, "variable_other_leg_scb");
        if (DOS_ADDR_INVALID(pszOtherLeg))
        {
            g_pstTaskMngtInfo->stStat.ulCurrentCalls--;

            if (pstGateway && DOS_FALSE != pstGateway->bStatus)
            {
                pstGateway->stStat.ulCurrentCalls--;
            }
        }

        /* 如果挂断时SIP的响应码不是2xx就认为失败的session,通道变量
           variable_proto_specific_hangup_cause格式为 sip:200 后面数字为sip错误码 */
        pszSIPHangupCause = esl_event_get_header(pstEvent, "variable_proto_specific_hangup_cause");
        if (DOS_ADDR_VALID(pszSIPHangupCause))
        {
            if (dos_strncmp(pszSIPHangupCause, "sip:2", dos_strlen("sip:2")) != 0)
            {
                g_pstTaskMngtInfo->stStat.ulFailSessions++;
                if (pstGateway && DOS_FALSE != pstGateway->bStatus)
                {
                    pstGateway->stStat.ulFailSessions++;
                }
            }
        }
    }

    pthread_mutex_unlock(&g_mutexHashGW);


    return DOS_SUCC;
}

/**
 * 函数: U32 sc_ep_channel_park_proc(esl_handle_t *pstHandle, esl_event_t *pstEvent, SC_SCB_ST *pstSCB)
 * 功能: 处理ESL的CHANNEL PARK事件
 * 参数:
 *      esl_handle_t *pstHandle : 发送句柄
 *      esl_event_t *pstEvent   : 时间
 *      SC_SCB_ST *pstSCB       : SCB
 * 返回值: 成功返回DOS_SUCC，否则返回DOS_FAIL
 */
U32 sc_ep_channel_park_proc(esl_handle_t *pstHandle, esl_event_t *pstEvent, SC_SCB_ST *pstSCB)
{
    S8        *pszIsAutoCall = NULL;
    S8        *pszCaller     = NULL;
    S8        *pszCallee     = NULL;
    S8        *pszUUID       = NULL;
    S8        *pszMainService = NULL;
    S8        *pszTransfor    = NULL;
    U32       ulCallSrc, ulCallDst;
    U32       ulRet = DOS_SUCC;
    U32       ulMainService = U32_BUTT;
    SC_SCB_ST *pstSCBOther  = NULL;
    SC_SCB_ST *pstSCBNew    = NULL;

    if (DOS_ADDR_INVALID(pstEvent)
        || DOS_ADDR_INVALID(pstHandle)
        || DOS_ADDR_INVALID(pstSCB))
    {
        DOS_ASSERT(0);

        SC_TRACE_OUT();
        return DOS_FAIL;
    }

    sc_logr_debug(SC_ESL, "Start process event %s.", esl_event_get_header(pstEvent, "Event-Name"));

    /*  1.申请控制块
     *  2.判断是否是自动外呼
     *    如果是自动外呼: 就使用originate命令发起呼叫
     *  3.星业务
     *    执行相应业务
     *  4.普通呼叫
     *    查找路由，找中级组，或者对应的SIP分机呼叫
     */

    /* 获取事件的UUID */
    pszUUID = esl_event_get_header(pstEvent, "Caller-Unique-ID");
    if (DOS_ADDR_INVALID(pszUUID))
    {
        DOS_ASSERT(0);

        SC_TRACE_OUT();
        return DOS_FAIL;
    }

    /* 业务控制 */
    pszIsAutoCall = esl_event_get_header(pstEvent, "variable_auto_call");
    pszCaller     = esl_event_get_header(pstEvent, "Caller-Caller-ID-Number");
    pszCallee     = esl_event_get_header(pstEvent, "Caller-Destination-Number");
    sc_logr_info(SC_ESL, "Route Call Start: Auto Call Flag: %s, Caller: %s, Callee: %s"
                , NULL == pszIsAutoCall ? "NULL" : pszIsAutoCall
                , NULL == pszCaller ? "NULL" : pszCaller
                , NULL == pszCallee ? "NULL" : pszCallee);

    pszMainService = esl_event_get_header(pstEvent, "variable_main_service");
    if (DOS_ADDR_INVALID(pszMainService)
        || dos_atoul(pszMainService, &ulMainService) < 0)
    {
        ulMainService = U32_BUTT;
    }

    pszTransfor = esl_event_get_header(pstEvent, "Caller-Transfer-Source");
    if (DOS_ADDR_VALID(pszTransfor) && '\0' != pszTransfor[0])
    {
        if (dos_strstr(pszTransfor, "bl_xfer"))
        {
            ulMainService = SC_SERV_BLIND_TRANSFER;
        }
        else if (dos_strstr(pszTransfor, "att_xfer"))
        {
            ulMainService = SC_SERV_ATTEND_TRANSFER;
        }

        sc_logr_info(SC_ESL, "Call transfor prepare finished. Start transfor call. Callee: %s", pstSCB->szCalleeNum);

        ulRet = sc_ep_call_transfer(pstSCB, pszCallee, (SC_SERV_ATTEND_TRANSFER == ulMainService));

        goto proc_finished;
    }

    if (SC_SERV_CALL_INTERCEPT == ulMainService
        || SC_SERV_CALL_WHISPERS == ulMainService)
    {
        ulRet = sc_ep_call_intercept(pstSCB);
    }
    else if (SC_SERV_ATTEND_TRANSFER == ulMainService
        || SC_SERV_BLIND_TRANSFER == ulMainService)
    {
        ulRet = sc_ep_transfer_publish_active(pstSCB);
    }
    else if (SC_SERV_AGENT_CLICK_CALL == ulMainService)
    {
        ulRet = sc_ep_call_agent_by_id(pstSCB, pstSCB->ulAgentID);
    }
    else if (SC_SERV_PREVIEW_DIALING == ulMainService)
    {
        if (pstSCB->usOtherSCBNo >= SC_MAX_SCB_NUM)
        {
            /* 呼叫客户 */
            pstSCBNew = sc_scb_alloc();
            if (DOS_ADDR_INVALID(pstSCB))
            {
                sc_logr_warning(SC_ESL, "%s", "Cannot make call for the API CMD. Alloc SCB FAIL.");
                ulRet = DOS_FAIL;
                DOS_ASSERT(0);
                goto proc_finished;
            }

            pstSCBNew->ulCustomID = pstSCB->ulCustomID;
            pstSCBNew->ulAgentID = pstSCB->ulAgentID;
            pstSCBNew->ulTaskID = pstSCB->ulTaskID;
            pstSCBNew->usOtherSCBNo = pstSCB->usSCBNo;
            pstSCB->usOtherSCBNo = pstSCBNew->usSCBNo;

            /* 指定被叫号码 */
            dos_strncpy(pstSCBNew->szCalleeNum, pszCaller, sizeof(pstSCBNew->szCalleeNum));
            pstSCBNew->szCalleeNum[sizeof(pstSCBNew->szCalleeNum) - 1] = '\0';

            /* 指定主叫号码 TODO 号码组!! */
            dos_strncpy(pstSCBNew->szCallerNum, pszCallee, sizeof(pstSCBNew->szCallerNum));
            pstSCBNew->szCallerNum[sizeof(pstSCBNew->szCallerNum) - 1] = '\0';

            SC_SCB_SET_SERVICE(pstSCBNew, SC_SERV_PREVIEW_DIALING);

            if (!sc_ep_black_list_check(pstSCBNew->ulCustomID, pszCaller))
            {
                DOS_ASSERT(0);

                sc_logr_info(SC_ESL, "Cannot make call. Callee in blocak list. (%s)", pszCallee);
                ulRet = DOS_FAIL;
                goto proc_finished;
            }

            SC_SCB_SET_SERVICE(pstSCB, SC_SERV_OUTBOUND_CALL);
            SC_SCB_SET_SERVICE(pstSCB, SC_SERV_EXTERNAL_CALL);
            if (sc_send_usr_auth2bs(pstSCBNew) != DOS_SUCC)
            {
                DOS_ASSERT(0);

                sc_logr_info(SC_ESL, "Cannot make call. Send auth fail.", pszCallee);
                ulRet = DOS_FAIL;
                goto proc_finished;
            }

        }
    }
    /* 如果是AUTO Call就不需要创建SCB，将SCB同步到HASH表中就好 */
    else if (SC_SERV_AUTO_DIALING == ulMainService)
    {
        /* 自动外呼处理 */
        ulRet = sc_ep_auto_dial_proc(pstHandle, pstEvent, pstSCB);
    }
    else if (SC_SERV_NUM_VERIFY == ulMainService)
    {
        ulRet = sc_ep_num_verify(pstHandle, pstEvent, pstSCB);
    }
    /* 如果是回呼到坐席的呼叫。就需要连接客户和坐席 */
    else if (SC_SERV_AGENT_CALLBACK == ulMainService
        || SC_SERV_OUTBOUND_CALL == ulMainService)
    {
        S8 szCMDBuff[512] = { 0, };
        /*
        pszOtherSCBNo = esl_event_get_header(pstEvent, "variable_other_leg_scb");
        if (DOS_ADDR_INVALID(pszOtherSCBNo)
            || dos_atoul(pszOtherSCBNo, &ulOtherSCBNo) < 0)
        {
            DOS_ASSERT(0);

            sc_ep_hangup_call(pstSCB, BS_TERM_INTERNAL_ERR);
            ulRet = DOS_FAIL;

            goto proc_finished;
        }
        */
        pstSCBOther = sc_scb_get(pstSCB->usOtherSCBNo);
        if (DOS_ADDR_INVALID(pstSCBOther))
        {
            DOS_ASSERT(0);

            sc_ep_hangup_call(pstSCB, BS_TERM_INTERNAL_ERR);
            ulRet = DOS_FAIL;

            goto proc_finished;
        }

        /* 如果命令执行失败，就需要挂断另外一通呼叫 */
        dos_snprintf(szCMDBuff, sizeof(szCMDBuff), "bgapi uuid_bridge %s %s \r\n", pstSCB->szUUID, pstSCBOther->szUUID);
        pstSCBOther->usOtherSCBNo= pstSCB->usSCBNo;
        pstSCB->usOtherSCBNo = pstSCBOther->usSCBNo;

        if (sc_ep_esl_execute_cmd(szCMDBuff) != DOS_SUCC)
        {
            sc_ep_hangup_call(pstSCBOther, BS_TERM_INTERNAL_ERR);
            sc_ep_hangup_call(pstSCB, BS_TERM_INTERNAL_ERR);
            ulRet = DOS_FAIL;

            goto proc_finished;

        }

        SC_SCB_SET_STATUS(pstSCB, SC_SCB_ACTIVE);

        sc_logr_info(SC_ESL, "Agent has benn connected. UUID: %s <> %s. SCBNo: %d <> %d."
                     , pstSCB->szUUID, pstSCBOther->szUUID
                     , pstSCB->usSCBNo, pstSCBOther->usSCBNo);
    }
    else if (sc_ep_internal_service_check(pstEvent) != SC_INTER_SRV_BUTT)
    {
        /* 接听主叫方呼叫 */
        sc_ep_esl_execute("answer", NULL, pszUUID);

        /* 内部业务处理 */
        SC_SCB_SET_SERVICE(pstSCB, SC_SERV_INBOUND_CALL);
        SC_SCB_SET_SERVICE(pstSCB, SC_SERV_INTERNAL_CALL);
        SC_SCB_SET_SERVICE(pstSCB, SC_SERV_INTERNAL_SERVICE);

        ulRet = sc_ep_internal_service_proc(pstHandle, pstEvent, pstSCB);
    }
    else
    {
        /* 正常呼叫处理 */
        pstSCB->ucLegRole = SC_CALLEE;
        ulCallSrc = sc_ep_get_source(pstEvent);
        ulCallDst = sc_ep_get_destination(pstEvent);

        /* 获得ulCustomID */
        if (SC_DIRECTION_SIP == ulCallSrc && SC_DIRECTION_PSTN == ulCallDst)
        {
            pstSCB->ulCustomID = sc_ep_get_custom_by_sip_userid(pstSCB->szCallerNum);
        }
        else if (SC_DIRECTION_PSTN == ulCallSrc && SC_DIRECTION_SIP == ulCallDst)
        {
            pstSCB->ulCustomID = sc_ep_get_custom_by_did(pstSCB->szCalleeNum);
        }
        else if (SC_DIRECTION_SIP == ulCallSrc && SC_DIRECTION_SIP == ulCallDst)
        {
            pstSCB->ulCustomID = sc_ep_get_custom_by_sip_userid(pszCaller);
        }
        else
        {
            DOS_ASSERT(0);
            sc_logr_info(SC_ESL, "Invalid call source or destension. Source: %d, Dest: %d", ulCallSrc, ulCallDst);

            ulRet = DOS_FAIL;

            goto proc_finished;
        }

        /* 判断SIP是否属于企业 */
        if (sc_ep_customer_list_find(pstSCB->ulCustomID) != DOS_SUCC)
        {
            SC_SCB_SET_STATUS(pstSCB, SC_SCB_RELEASE);
            ulRet = DOS_FAIL;
            sc_logr_info(SC_ESL, "Invalid call source or destension. Source: %d, Dest: %d, CustomID(%u) is not vest in enterprise", ulCallSrc, ulCallDst, pstSCB->ulCustomID);

            goto proc_finished;
        }

        sc_logr_info(SC_ESL, "Get call source and dest. Source: %d, Dest: %d", ulCallSrc, ulCallDst);

        if (SC_DIRECTION_SIP == ulCallSrc && SC_DIRECTION_PSTN == ulCallDst)
        {
            SC_SCB_SET_SERVICE(pstSCB, SC_SERV_INBOUND_CALL);
            SC_SCB_SET_SERVICE(pstSCB, SC_SERV_INTERNAL_CALL);

            /* 更改不同的主叫，获取当前呼叫时哪一个客户 */
            if (U32_BUTT != pstSCB->ulCustomID)
            {
                /* 根据SIP，找到坐席，将SCB的usSCBNo, 绑定到坐席上 */
                if (sc_acd_update_agent_scbno(pstSCB->szCallerNum, pstSCB->usSCBNo, DOS_FALSE) != DOS_SUCC)
                {
                    sc_logr_info(SC_ESL, "update agent SCBNO FAIL. sip : %s, SCBNO : %d", pstSCB->szCallerNum, pstSCB->usSCBNo);
                }
                else
                {
                    sc_logr_debug(SC_ESL, "update agent SCBNO SUCC. sip : %s, SCBNO : %d", pstSCB->szCallerNum, pstSCB->usSCBNo);
                }

                if (sc_ep_outgoing_call_proc(pstSCB) != DOS_SUCC)
                {
                    SC_SCB_SET_STATUS(pstSCB, SC_SCB_RELEASE);
                    ulRet = DOS_FAIL;
                }
                else
                {
                    SC_SCB_SET_STATUS(pstSCB, SC_SCB_EXEC);
                }
            }
            else
            {
                pstSCB->ucTerminationFlag = DOS_TRUE;
                pstSCB->ucTerminationCause = BS_ERR_SYSTEM;

                sc_ep_hangup_call(pstSCB, BS_TERM_CUSTOM_INVALID);

                SC_SCB_SET_STATUS(pstSCB, SC_SCB_RELEASE);
                ulRet = DOS_FAIL;
            }
        }
        else if (SC_DIRECTION_PSTN == ulCallSrc && SC_DIRECTION_SIP == ulCallDst)
        {
            SC_SCB_SET_SERVICE(pstSCB, SC_SERV_INBOUND_CALL);
            SC_SCB_SET_SERVICE(pstSCB, SC_SERV_EXTERNAL_CALL);

            if (pstSCB->ulCustomID != U32_BUTT)
            {
                if (sc_send_usr_auth2bs(pstSCB) != DOS_SUCC)
                {
                    sc_ep_hangup_call(pstSCB, BS_TERM_COMM_FAIL);

                    SC_SCB_SET_STATUS(pstSCB, SC_SCB_RELEASE);
                    ulRet = DOS_FAIL;
                }
                else
                {
                    SC_SCB_SET_STATUS(pstSCB, SC_SCB_AUTH);
                }
            }
            else
            {
                DOS_ASSERT(0);

                sc_ep_hangup_call(pstSCB, BS_TERM_CUSTOM_INVALID);

                SC_SCB_SET_STATUS(pstSCB, SC_SCB_RELEASE);
                ulRet = DOS_FAIL;
            }
        }
        else if (SC_DIRECTION_SIP == ulCallSrc && SC_DIRECTION_SIP == ulCallDst)
        {
            SC_SCB_SET_SERVICE(pstSCB, SC_SERV_INTERNAL_CALL);
            /* 根据SIP，找到坐席，将SCB的usSCBNo, 绑定到坐席上 */
            if (sc_acd_update_agent_scbno(pszCaller, pstSCB->usSCBNo, DOS_FALSE) != DOS_SUCC)
            {
                sc_logr_info(SC_ESL, "update agent SCBNO FAIL. sip : %s, SCBNO : %d", pszCaller, pstSCB->usSCBNo);
            }
            else
            {
                sc_logr_debug(SC_ESL, "update agent SCBNO SUCC. sip : %s, SCBNO : %d", pszCaller, pstSCB->usSCBNo);
            }

            ulRet = sc_ep_internal_call_process(pstHandle, pstEvent, pstSCB);
        }
        else
        {
            DOS_ASSERT(0);
            sc_logr_info(SC_ESL, "Invalid call source or destension. Source: %d, Dest: %d", ulCallSrc, ulCallDst);

            ulRet = DOS_FAIL;
        }
    }

proc_finished:
    sc_call_trace(pstSCB, "Finished to process %s event. Result : %s"
                    , esl_event_get_header(pstEvent, "Event-Name")
                    , (DOS_SUCC == ulRet) ? "OK" : "FAILED");

    return ulRet;
}

U32 sc_ep_backgroud_job_proc(esl_handle_t *pstHandle, esl_event_t *pstEvent)
{
    S8    *pszEventBody   = NULL;
    S8    *pszAppName     = NULL;
    S8    *pszAppArg      = NULL;
    S8    *pszStart       = NULL;
    S8    *pszEnd         = NULL;
    S8    szSCBNo[16]      = { 0 };
    U32   ulProcessResult = DOS_SUCC;
    U32   ulSCBNo    = 0;
    SC_SCB_ST   *pstSCB = NULL;
    SC_SCB_ST   *pstOtherSCB = NULL;

    if (DOS_ADDR_INVALID(pstHandle)

        || DOS_ADDR_INVALID(pstEvent))
    {
        DOS_ASSERT(0);
        return DOS_FAIL;
    }

    pszEventBody = esl_event_get_body(pstEvent);
    pszAppName   = esl_event_get_header(pstEvent, "Job-Command");
    pszAppArg    = esl_event_get_header(pstEvent, "Job-Command-Arg");
    if (DOS_ADDR_VALID(pszEventBody)
        && DOS_ADDR_VALID(pszAppName)
        && DOS_ADDR_VALID(pszAppArg))
    {
        if (0 == dos_strnicmp(pszEventBody, "+OK", dos_strlen("+OK")))
        {
            ulProcessResult = DOS_SUCC;
        }
        else
        {
            ulProcessResult = DOS_FAIL;
        }

        sc_logr_info(SC_ESL, "Execute command %s %s %s, Info: %s."
                        , pszAppName
                        , pszAppArg
                        , DOS_SUCC == ulProcessResult ? "SUCC" : "FAIL"
                        , pszEventBody);

        if (DOS_SUCC == ulProcessResult)
        {
            goto process_finished;
        }

        DOS_ASSERT(0);

        pszStart = dos_strstr(pszAppArg, "scb_number=");
        if (DOS_ADDR_INVALID(pszStart))
        {
            DOS_ASSERT(0);
            goto process_fail;
        }

        pszStart += dos_strlen("scb_number=");
        if (DOS_ADDR_INVALID(pszStart))
        {
            DOS_ASSERT(0);
            goto process_fail;
        }

        pszEnd = dos_strstr(pszStart, ",");
        if (DOS_ADDR_VALID(pszEnd))
        {
            dos_strncpy(szSCBNo, pszStart, pszEnd-pszStart);
            szSCBNo[pszEnd-pszStart] = '\0';
        }
        else
        {
            dos_strncpy(szSCBNo, pszStart, sizeof(szSCBNo));
            szSCBNo[sizeof(szSCBNo)-1] = '\0';
        }

        if (dos_atoul(szSCBNo, &ulSCBNo) < 0)
        {
            DOS_ASSERT(0);
            goto process_fail;
        }

        pstSCB = sc_scb_get(ulSCBNo);
        if (DOS_ADDR_VALID(pstSCB))
        {
            pstOtherSCB = sc_scb_get(pstSCB->usOtherSCBNo);
        }

        if (dos_stricmp(pszAppName, "originate") == 0)
        {
            if (DOS_ADDR_VALID(pstOtherSCB))
            {
                /* 这个地方需要视呼叫业务进行处理 */
                sc_ep_esl_execute("hangup", NULL, pstSCB->szUUID);
            }
            else
            {
                /* 如果还没有创建通道就释放控制块 */
                if (!pstSCB->bChannelCreated
                    && sc_bg_job_find(pstSCB->usSCBNo))
                {
                    /* 呼叫失败了 */
                    DOS_ASSERT(0);

                    sc_bg_job_hash_delete(pstSCB->usSCBNo);
                    sc_scb_free(pstSCB);
                    pstSCB = NULL;
                }
            }

            sc_logr_error(SC_ESL, "ERROR: BGJOB Fail.Argv: %s, SCB-NO: %s(%u)", pszAppArg, szSCBNo, ulSCBNo);
        }
    }

process_finished:
    return DOS_SUCC;

process_fail:
    return DOS_FAIL;
}


/**
 * 函数: U32 sc_ep_channel_create_proc(esl_handle_t *pstHandle, esl_event_t *pstEvent)
 * 功能: 处理ESL的CHANNEL CREATE事件
 * 参数:
 *      esl_event_t *pstEvent   : 时间
 *      esl_handle_t *pstHandle : 发送句柄
 * 返回值: 成功返回DOS_SUCC，否则返回DOS_FAIL
 */
U32 sc_ep_channel_create_proc(esl_handle_t *pstHandle, esl_event_t *pstEvent)
{
    S8          *pszUUID = NULL;
    S8          *pszMainService = NULL;
    S8          *pszSCBNum = NULL;
    S8          *pszOtherSCBNo = NULL;
    S8          *pszCaller = NULL;
    SC_SCB_ST   *pstSCB = NULL;
    SC_SCB_ST   *pstSCB1 = NULL;
    S8          szBuffCmd[128] = { 0 };
    U32         ulSCBNo = 0;
    U32         ulOtherSCBNo = 0;
    U32         ulRet = DOS_SUCC;
    U32         ulMainService = U32_BUTT;

    SC_TRACE_IN(pstEvent, pstHandle, pstSCB, 0);

    if (DOS_ADDR_INVALID(pstEvent)
        || DOS_ADDR_INVALID(pstHandle))
    {
        DOS_ASSERT(0);

        SC_TRACE_OUT();
        return DOS_FAIL;
    }

    sc_logr_debug(SC_ESL, "Start process event %s.", esl_event_get_header(pstEvent, "Event-Name"));

    pszUUID = esl_event_get_header(pstEvent, "Caller-Unique-ID");
    if (DOS_ADDR_INVALID(pszUUID))
    {
        DOS_ASSERT(0);

        SC_TRACE_OUT();
        return DOS_FAIL;
    }

    pszMainService = esl_event_get_header(pstEvent, "variable_main_service");
    if (DOS_ADDR_INVALID(pszMainService)
        || dos_atoul(pszMainService, &ulMainService) < 0)
    {
        ulMainService = U32_BUTT;
    }

    /* 如果是AUTO Call就不需要创建SCB，将SCB同步到HASH表中就好 */
    pszSCBNum = esl_event_get_header(pstEvent, "variable_scb_number");
    if (DOS_ADDR_VALID(pszSCBNum))
    {
        if (dos_atoul(pszSCBNum, &ulSCBNo) < 0)
        {
            DOS_ASSERT(0);

            goto process_fail;
        }

        pstSCB = sc_scb_get(ulSCBNo);
        if (DOS_ADDR_INVALID(pstSCB))
        {
            DOS_ASSERT(0);

            goto process_fail;
        }

        /* 更新UUID */
        dos_strncpy(pstSCB->szUUID, pszUUID, sizeof(pstSCB->szUUID));
        pstSCB->szUUID[sizeof(pstSCB->szUUID) - 1] = '\0';

		sc_scb_hash_tables_add(pszUUID, pstSCB);

        if (sc_call_check_service(pstSCB, SC_SERV_AUTO_DIALING)
            && pstSCB->usTCBNo < SC_MAX_TASK_NUM)
        {
            sc_task_concurrency_add(pstSCB->usTCBNo);
        }

        pstSCB->bChannelCreated = DOS_TRUE;

        goto process_finished;

process_fail:
       ulRet = DOS_FAIL;
    }
    else
    {
        pstSCB = sc_scb_alloc();
        if (DOS_ADDR_INVALID(pstSCB))
        {
            DOS_ASSERT(0);
            sc_logr_error(SC_ESL, "%s", "Alloc SCB FAIL.");

            SC_SCB_SET_STATUS(pstSCB, SC_SCB_RELEASE);

            SC_TRACE_OUT();
            return DOS_FAIL;
        }

        sc_scb_hash_tables_add(pszUUID, pstSCB);
        sc_ep_parse_event(pstEvent, pstSCB);

        dos_strncpy(pstSCB->szUUID, pszUUID, sizeof(pstSCB->szUUID));
        pstSCB->szUUID[sizeof(pstSCB->szUUID) - 1] = '\0';

        /* 给通道设置变量 */
        dos_snprintf(szBuffCmd, sizeof(szBuffCmd), "scb_number=%u", pstSCB->usSCBNo);
        sc_ep_esl_execute("set", szBuffCmd, pszUUID);

        SC_SCB_SET_STATUS(pstSCB, SC_SCB_INIT);

        pstSCB->bChannelCreated = DOS_TRUE;
#if 0
        /* 需要处理transfer业务 */
        pszRefer = esl_event_get_header(pstEvent, "variable_sip_h_Referred-By");
        if (DOS_ADDR_INVALID(pszRefer) || '\0' == pszRefer[0])
        {
            ulRet = DOS_SUCC;
            goto process_fail1;
        }

        pszOtherUUID = esl_event_get_header(pstEvent, "Other-Leg-Unique-ID");
        if (DOS_ADDR_INVALID(pszOtherUUID) || '\0' == pszOtherUUID[0])
        {
            DOS_ASSERT(0);
            ulRet = DOS_FAIL;
            goto process_fail1;
        }

        pstSCB1 = sc_scb_hash_tables_find(pszOtherUUID);
        if (DOS_ADDR_INVALID(pstSCB1))
        {
            DOS_ASSERT(0);
            ulRet = DOS_FAIL;
            goto process_fail1;
        }

        SC_SCB_SET_SERVICE(pstSCB, SC_SERV_BLIND_TRANSFER);
        pstSCB->usPublishSCB = pstSCB1->usSCBNo;
        pstSCB->ucTranforRole = SC_TRANS_ROLE_PUBLISH;

        pstSCB1->usOtherSCBNo = pstSCB->usSCBNo;
        pstSCB1->ucTranforRole = SC_TRANS_ROLE_NOTIFY;

        pstSCB1 = sc_scb_get(pstSCB1->usSCBNo);
        if (DOS_ADDR_INVALID(pstSCB1))
        {
            DOS_ASSERT(0);
            ulRet = DOS_FAIL;
            goto process_fail1;
        }

        pstSCB1->ucTranforRole = SC_TRANS_ROLE_SUBSCRIPTION;

        ulRet = DOS_SUCC;
process_fail1:
        /*  Do Nothing */;
#endif
    }


    /* 根据参数  交换SCB No */
    pszOtherSCBNo = esl_event_get_header(pstEvent, "variable_other_leg_scb");
    if (DOS_ADDR_INVALID(pszOtherSCBNo)
        && dos_atoul(pszOtherSCBNo, &ulOtherSCBNo) < 0)
    {
        goto process_finished;
    }

    pstSCB1 = sc_scb_get(ulOtherSCBNo);
    if (DOS_ADDR_VALID(pstSCB)
        && DOS_ADDR_VALID(pstSCB1))
    {
        pstSCB->usOtherSCBNo = pstSCB1->usSCBNo;
        pstSCB1->usOtherSCBNo = pstSCB->usSCBNo;
    }

     /* 根据参数update_agent，判断是否需要更新坐席中的 usSCBNo  */
    pszCaller = esl_event_get_header(pstEvent, "variable_update_agent");
    if (DOS_ADDR_VALID(pszCaller) && pszCaller[0] != '\0')
    {
        if (sc_acd_update_agent_scbno(pszCaller, pstSCB->usSCBNo, DOS_FALSE) != DOS_SUCC)
        {
            sc_logr_info(SC_ESL, "update agent SCBNO FAIL. sip : %s, SCBNO : %d!", pszCaller, pstSCB->usSCBNo);
        }
        else
        {
            sc_logr_debug(SC_ESL, "update agent SCBNO SUCC. sip : %s, SCBNO : %d!", pszCaller, pstSCB->usSCBNo);
        }
    }

process_finished:

    sc_call_trace(pstSCB, "Finished to process %s event.", esl_event_get_header(pstEvent, "Event-Name"));

    SC_TRACE_OUT();
    return ulRet;
}


/**
 * 函数: U32 sc_ep_channel_answer(esl_handle_t *pstHandle, esl_event_t *pstEvent, SC_SCB_ST *pstSCB)
 * 功能: 处理ESL的CHANNEL EXECUTE COMPLETE事件
 * 参数:
 *      esl_handle_t *pstHandle : 发送句柄
 *      esl_event_t *pstEvent   : 时间
 *      SC_SCB_ST *pstSCB       : SCB
 * 返回值: 成功返回DOS_SUCC，否则返回DOS_FAIL
 */
U32 sc_ep_channel_answer(esl_handle_t *pstHandle, esl_event_t *pstEvent, SC_SCB_ST *pstSCB)
{
    S8 *pszWaitingPark = NULL;
    S8 szFilePath[512] = { 0 };
    S8 szAPPParam[512] = { 0 };
    SC_SCB_ST *pstSCBOther = NULL;

    SC_TRACE_IN(pstEvent, pstHandle, pstSCB, 0);

    if (DOS_ADDR_INVALID(pstEvent)
        || DOS_ADDR_INVALID(pstHandle)
        || DOS_ADDR_INVALID(pstSCB))
    {
        DOS_ASSERT(0);

        SC_TRACE_OUT();
        return DOS_FAIL;
    }

    sc_call_trace(pstSCB, "Start process event %s.", esl_event_get_header(pstEvent, "Event-Name"));

    /* 如果没有置上waiting park标志，就直接切换状态到active */
    pszWaitingPark = esl_event_get_header(pstEvent, "variable_waiting_park");
    if (DOS_ADDR_INVALID(pszWaitingPark)
        || 0 != dos_strncmp(pszWaitingPark, "true", dos_strlen("true")))
    {
        SC_SCB_SET_STATUS(pstSCB, SC_SCB_ACTIVE);
    }

    if (U32_BUTT == pstSCB->ulCustomID)
    {
        pstSCBOther = sc_scb_get(pstSCB->usOtherSCBNo);
        if (DOS_ADDR_INVALID(pstSCBOther))
        {
            /* @TODO 要不要给挂断呼叫 ? */
        }
        else
        {
            pstSCB->ulCustomID = pstSCBOther->ulCustomID;
        }
    }

    if (sc_call_check_service(pstSCB, SC_SERV_AGENT_SIGNIN))
    {
        /* 这个地方要放音哦 */
        /* 更新坐席状态为connect */
        sc_acd_update_agent_status(SC_ACD_SITE_ACTION_CONNECTED, pstSCB->ulAgentID);
    }
    else
    {
        if (pstSCB->bRecord)
        {
            SC_SCB_SET_SERVICE(pstSCB, BS_SERV_RECORDING);
            sc_get_record_file_path(szFilePath, sizeof(szFilePath), pstSCB->ulCustomID, pstSCB->szCallerNum, pstSCB->szCalleeNum);
            pthread_mutex_lock(&pstSCB->mutexSCBLock);
            pstSCB->pszRecordFile = dos_dmem_alloc(dos_strlen(szFilePath) + 1);
            if (DOS_ADDR_VALID(pstSCB->pszRecordFile))
            {
                dos_strncpy(pstSCB->pszRecordFile, szFilePath, dos_strlen(szFilePath) + 1);
                pstSCB->pszRecordFile[dos_strlen(szFilePath)] = '\0';

                dos_snprintf(szAPPParam, sizeof(szAPPParam)
                                , "bgapi uuid_record %s start %s/%s\r\n"
                                , pstSCB->szUUID
                                , SC_RECORD_FILE_PATH
                                , szFilePath);
                sc_ep_esl_execute_cmd(szAPPParam);
                sc_ep_esl_execute("sleep", "300", pstSCB->szUUID);
            }
            else
            {
                DOS_ASSERT(0);
            }
            pthread_mutex_unlock(&pstSCB->mutexSCBLock);
        }
    }

    sc_call_trace(pstSCB, "Finished to process %s event.", esl_event_get_header(pstEvent, "Event-Name"));

    SC_TRACE_OUT();
    return DOS_SUCC;
}

/**
 * 函数: U32 sc_ep_channel_hungup_proc(esl_handle_t *pstHandle, esl_event_t *pstEvent, SC_SCB_ST *pstSCB)
 * 功能: 处理ESL的CHANNEL HANGUP事件
 * 参数:
 *      esl_handle_t *pstHandle : 发送句柄
 *      esl_event_t *pstEvent   : 时间
 *      SC_SCB_ST *pstSCB       : SCB
 * 返回值: 成功返回DOS_SUCC，否则返回DOS_FAIL
 */
U32 sc_ep_channel_hungup_proc(esl_handle_t *pstHandle, esl_event_t *pstEvent, SC_SCB_ST *pstSCB)
{
    SC_TRACE_IN(pstEvent, pstHandle, pstSCB, 0);

    if (DOS_ADDR_INVALID(pstEvent)
        || DOS_ADDR_INVALID(pstHandle)
        || DOS_ADDR_INVALID(pstSCB))
    {
        DOS_ASSERT(0);

        SC_TRACE_OUT();
        return DOS_FAIL;
    }

    sc_call_trace(pstSCB, "Start process event %s.", esl_event_get_header(pstEvent, "Event-Name"));

    SC_SCB_SET_STATUS(pstSCB, SC_SCB_RELEASE);

    sc_call_trace(pstSCB, "Finished to process %s event.", esl_event_get_header(pstEvent, "Event-Name"));

    SC_TRACE_OUT();
    return DOS_SUCC;

}

/**
 * 函数: U32 sc_ep_channel_hungup_complete_proc(esl_handle_t *pstHandle, esl_event_t *pstEvent, SC_SCB_ST *pstSCB)
 * 功能: 处理ESL的CHANNEL HANGUP COMPLETE事件
 * 参数:
 *      esl_handle_t *pstHandle : 发送句柄
 *      esl_event_t *pstEvent   : 时间
 *      SC_SCB_ST *pstSCB       : SCB
 * 返回值: 成功返回DOS_SUCC，否则返回DOS_FAIL
 */
U32 sc_ep_channel_hungup_complete_proc(esl_handle_t *pstHandle, esl_event_t *pstEvent, SC_SCB_ST *pstSCB)
{
    U32         ulStatus, ulRet = DOS_SUCC;
    S8          szCMD[512] = { 0, };
    S8          *pszTransforType = NULL;
    S8          *pszGatewayID = NULL;
    SC_SCB_ST   *pstSCBOther = NULL;

    SC_TRACE_IN(pstEvent, pstHandle, pstSCB, 0);

    if (DOS_ADDR_INVALID(pstEvent)
        || DOS_ADDR_INVALID(pstHandle)
        || DOS_ADDR_INVALID(pstSCB))
    {
        DOS_ASSERT(0);

        SC_TRACE_OUT();
        return DOS_FAIL;
    }

    pszGatewayID = esl_event_get_header(pstEvent, "variable_sip_gateway_name");
    if (DOS_ADDR_VALID(pszGatewayID)
        || dos_atoul(pszGatewayID, &pstSCB->ulTrunkID) < 0)
    {
        pstSCB->ulTrunkID = U32_BUTT;
    }

    pszTransforType = esl_event_get_header(pstEvent, "variable_transfer_to");
    if (DOS_ADDR_VALID(pszTransforType) && '\0' != pszTransforType[0])
    {
        if (dos_strnicmp(pszTransforType, "blind", dos_strlen("blind")) == 0
            || dos_strnicmp(pszTransforType, "attend", dos_strlen("attend")) == 0)
        {
            sc_logr_info(SC_ESL, "Got into transfor. transfor type: %s", pszTransforType);

            pstSCB->bWaitingOtherRelase = DOS_TRUE;
            goto process_finished;
        }
    }

    ulStatus = pstSCB->ucStatus;
    switch (ulStatus)
    {
        case SC_SCB_IDEL:
            /* 这个地方初始化一下就好 */
            DOS_ASSERT(0);

            sc_scb_hash_tables_delete(pstSCB->szUUID);
            sc_bg_job_hash_delete(pstSCB->usSCBNo);

            sc_scb_free(pstSCB);
            break;

        case SC_SCB_INIT:
        case SC_SCB_AUTH:
        case SC_SCB_EXEC:
        case SC_SCB_ACTIVE:
        case SC_SCB_RELEASE:
            /* 统一将资源置为release状态 */
            SC_SCB_SET_STATUS(pstSCB, SC_SCB_RELEASE);

            /* 将当前leg的信息dump下来 */
            pstSCB->pstExtraData = dos_dmem_alloc(sizeof(SC_SCB_EXTRA_DATA_ST));
            pthread_mutex_lock(&pstSCB->mutexSCBLock);
            if (DOS_ADDR_VALID(pstSCB->pstExtraData))
            {
                dos_memzero(pstSCB->pstExtraData, sizeof(SC_SCB_EXTRA_DATA_ST));
                sc_ep_parse_extra_data(pstEvent, pstSCB);
            }
/*
            if (pstSCB->pstExtraData->ulRingTimeStamp == 0)
            {
                pthread_mutex_unlock(&pstSCB->mutexSCBLock);

                goto process_finished;
            }
*/
            pthread_mutex_unlock(&pstSCB->mutexSCBLock);

            if (SC_TRANS_ROLE_NOTIFY == pstSCB->ucTranforRole)
            {
                if (sc_ep_transfer_notify_release(pstSCB) == DOS_SUCC)
                {
                    goto process_finished;
                }
            }
            else if (SC_TRANS_ROLE_PUBLISH == pstSCB->ucTranforRole)
            {
                if (sc_ep_transfer_publish_release(pstSCB) == DOS_SUCC)
                {
                    goto process_finished;
                }
            }
            else if (SC_TRANS_ROLE_SUBSCRIPTION == pstSCB->ucTranforRole)
            {
                if (sc_ep_transfer_subscription_release(pstSCB) == DOS_SUCC)
                {
                    goto process_finished;
                }
            }

            /* 如果呼叫已经进入队列了，需要删除 */
            if (pstSCB->bIsInQueue)
            {
                sc_cwq_del_call(pstSCB);
                pstSCB->bIsInQueue = DOS_FALSE;
            }

            /* 如果是呼叫坐席的，需要做特殊处理,看看坐席是否长连什么的 */
            if (pstSCB->bIsAgentCall)
            {
                sc_acd_agent_update_status(pstSCB->ulAgentID, SC_ACD_IDEL, U32_BUTT);
                pstSCB->bIsAgentCall = DOS_FALSE;
            }

            if (sc_call_check_service(pstSCB, SC_SERV_AGENT_SIGNIN))
            {
                sc_acd_update_agent_status(SC_ACD_SITE_ACTION_DISCONNECT, pstSCB->ulAgentID);
            }

            if (pstSCB->bRecord && pstSCB->pszRecordFile)
            {
                dos_snprintf(szCMD, sizeof(szCMD)
                                , "%s/%s-in.%s"
                                , SC_RECORD_FILE_PATH
                                , pstSCB->pszRecordFile
                                , esl_event_get_header(pstEvent, "Channel-Read-Codec-Name"));
                chown(szCMD, SC_NOBODY_UID, SC_NOBODY_GID);

                dos_snprintf(szCMD, sizeof(szCMD)
                                , "%s/%s-out.%s"
                                , SC_RECORD_FILE_PATH
                                , pstSCB->pszRecordFile
                                , esl_event_get_header(pstEvent, "Channel-Write-Codec-Name"));
                chown(szCMD, SC_NOBODY_UID, SC_NOBODY_GID);

            }

            /*
             * 1.如果有另外一条腿，有必要等待另外一条腿释放
             * 2.需要另外一条腿没有处于等待释放状态，那就等待吧
             */
            pstSCBOther = sc_scb_get(pstSCB->usOtherSCBNo);
            if (DOS_ADDR_VALID(pstSCBOther)
                && !pstSCBOther->bWaitingOtherRelase)
            {
                /* 长签就不挂断了 */
                if (!sc_call_check_service(pstSCBOther, SC_SERV_AGENT_SIGNIN))
                {
                    sc_ep_hangup_call(pstSCBOther, BS_TERM_HANGUP);

                    pstSCB->bWaitingOtherRelase = DOS_TRUE;

                    sc_logr_info(SC_ESL, "Waiting other leg hangup.Curretn Leg UUID: %s, Other Leg UUID: %s"
                                    , pstSCB->szUUID ? pstSCB->szUUID : "NULL"
                                    , pstSCBOther->szUUID ? pstSCBOther->szUUID : "NULL");

                    break;
                }
                else
                {
                    /* unbridge, 给坐席放音 */
                    dos_snprintf(szCMD, sizeof(szCMD), "uuid_break %s", pstSCBOther->szUUID);
                }
            }

            /* 自动外呼，需要维护任务的并发量 */
            if (sc_call_check_service(pstSCB, SC_SERV_AUTO_DIALING)
                && pstSCB->usTCBNo < SC_MAX_TASK_NUM)
            {
                sc_task_concurrency_minus(pstSCB->usTCBNo);

                sc_update_callee_status(pstSCB->usTCBNo, pstSCB->szCalleeNum, SC_CALLEE_NORMAL);
            }

            sc_logr_debug(SC_ESL, "Send CDR to bs. SCB1 No:%d, SCB2 No:%d", pstSCB->usSCBNo, pstSCB->usOtherSCBNo);
            /* 发送话单 */
            if (sc_send_billing_stop2bs(pstSCB) != DOS_SUCC)
            {
                sc_logr_debug(SC_ESL, "Send CDR to bs FAIL. SCB1 No:%d, SCB2 No:%d", pstSCB->usSCBNo, pstSCB->usOtherSCBNo);
            }
            else
            {
                sc_logr_debug(SC_ESL, "Send CDR to bs SUCC. SCB1 No:%d, SCB2 No:%d", pstSCB->usSCBNo, pstSCB->usOtherSCBNo);
            }

            sc_logr_debug(SC_ESL, "Start release the SCB. SCB1 No:%d, SCB2 No:%d", pstSCB->usSCBNo, pstSCB->usOtherSCBNo);
            /* 维护资源 */

            sc_scb_hash_tables_delete(pstSCB->szUUID);
            if (DOS_ADDR_VALID(pstSCBOther))
            {
                sc_scb_hash_tables_delete(pstSCBOther->szUUID);
            }

            sc_bg_job_hash_delete(pstSCB->usSCBNo);
            sc_scb_free(pstSCB);
            if (pstSCBOther)
            {
                sc_bg_job_hash_delete(pstSCBOther->usSCBNo);
                sc_scb_free(pstSCBOther);
            }
            break;
        default:
            DOS_ASSERT(0);
            ulRet = DOS_FAIL;
            break;
    }

process_finished:

    sc_call_trace(pstSCB, "Finished to process %s event.", esl_event_get_header(pstEvent, "Event-Name"));

    SC_TRACE_OUT();
    return DOS_SUCC;

}

/**
 * 函数: U32 sc_ep_channel_hold(esl_handle_t *pstHandle, esl_event_t *pstEvent, SC_SCB_ST *pstSCB)
 * 功能: 处理ESL的CHANNEL HEARTBEAT事件
 * 参数:
 *      esl_handle_t *pstHandle : 发送句柄
 *      esl_event_t *pstEvent   : 事件
 *      SC_SCB_ST *pstSCB       : SCB
 * 返回值: 成功返回DOS_SUCC，否则返回DOS_FAIL
 */
U32 sc_ep_channel_hold(esl_handle_t *pstHandle, esl_event_t *pstEvent, SC_SCB_ST *pstSCB)
{
    S8 *pszChannelStat = NULL;

    SC_TRACE_IN(pstEvent, pstHandle, pstSCB, 0);

    if (DOS_ADDR_INVALID(pstEvent)
        || DOS_ADDR_INVALID(pstHandle)
        || DOS_ADDR_INVALID(pstSCB))
    {
        DOS_ASSERT(0);

        SC_TRACE_OUT();
        /* Hold 事件不能拆呼叫的 */
        return DOS_SUCC;
    }

    sc_call_trace(pstSCB, "Start process event %s.", esl_event_get_header(pstEvent, "Event-Name"));

    pszChannelStat = esl_event_get_header(pstEvent, "Channel-Call-State");
    if (DOS_ADDR_VALID(pszChannelStat))
    {
        if (dos_strnicmp(pszChannelStat, "HELD", dos_strlen("HELD")) == 0)
        {
            if (0 == pstSCB->ulLastHoldTimetamp)
            {
                pstSCB->ulLastHoldTimetamp = time(0);
                pstSCB->usHoldCnt++;
            }
        }
        else if (dos_strnicmp(pszChannelStat, "ACTIVE", dos_strlen("ACTIVE")) == 0)
        {
            if (pstSCB->ulLastHoldTimetamp)
            {
                pstSCB->usHoldTotalTime += (time(0) - pstSCB->ulLastHoldTimetamp);
                pstSCB->ulLastHoldTimetamp = 0;
                pstSCB->usHoldCnt++;
            }
        }
    }

    sc_call_trace(pstSCB, "Finished to process %s event.", esl_event_get_header(pstEvent, "Event-Name"));

    SC_TRACE_OUT();
    return DOS_SUCC;
}

U32 sc_ep_update_corpclients(U32 ulCustomID, S32 lKey)
{
    S8 szSQL[512] = { 0 };

    dos_snprintf(szSQL, sizeof(szSQL), "UPDATE tbl_corpclients SET type=%d WHERE customer_id=%d", lKey, ulCustomID);

    return db_query(g_pstSCDBHandle, szSQL, NULL, NULL, NULL);
}

/**
 * 函数: U32 sc_ep_dtmf_proc(esl_handle_t *pstHandle, esl_event_t *pstEvent, SC_SCB_ST *pstSCB)
 * 功能: 处理ESL的CHANNEL DTMF事件
 * 参数:
 *      esl_handle_t *pstHandle : 发送句柄
 *      esl_event_t *pstEvent   : 时间
 *      SC_SCB_ST *pstSCB       : SCB
 * 返回值: 成功返回DOS_SUCC，否则返回DOS_FAIL
 */
U32 sc_ep_dtmf_proc(esl_handle_t *pstHandle, esl_event_t *pstEvent, SC_SCB_ST *pstSCB)
{
    S8 *pszDTMFDigit = NULL;
    U32 ulTaskMode   = U32_BUTT;
    S32 lKey         = 0;
    SC_SCB_ST *pstSCBOther = NULL;
    SC_TRACE_IN(pstEvent, pstHandle, pstSCB, 0);

    if (DOS_ADDR_INVALID(pstEvent)
        || DOS_ADDR_INVALID(pstHandle)
        || DOS_ADDR_INVALID(pstSCB))
    {
        DOS_ASSERT(0);

        SC_TRACE_OUT();
        /* dtmf 事件不能拆呼叫 */
        return DOS_SUCC;
    }

    sc_call_trace(pstSCB, "Start process event %s.", esl_event_get_header(pstEvent, "Event-Name"));

    pszDTMFDigit = esl_event_get_header(pstEvent, "DTMF-Digit");
    if (DOS_ADDR_INVALID(pszDTMFDigit))
    {
        DOS_ASSERT(0);
        goto process_fail;
    }

    /* 自动外呼，拨0接通坐席 */
    if (sc_call_check_service(pstSCB, SC_SERV_AUTO_DIALING))
    {

        ulTaskMode = sc_task_get_mode(pstSCB->usTCBNo);
        if (ulTaskMode >= SC_TASK_MODE_BUTT)
        {
            DOS_ASSERT(0);
            /* 要不要挂断 ? */
            goto process_fail;
        }

        if (SC_TASK_MODE_KEY4AGENT == ulTaskMode)
        {
            sc_ep_call_queue_add(pstSCB, sc_task_get_agent_queue(pstSCB->usTCBNo));
        }
        else if(SC_TASK_MODE_KEY4AGENT1 == ulTaskMode
                && '0' == pszDTMFDigit[0])
        {
            sc_ep_call_queue_add(pstSCB, sc_task_get_agent_queue(pstSCB->usTCBNo));
        }
    }
    else if (sc_call_check_service(pstSCB, SC_SERV_AGENT_CALLBACK))
    {
        /* AGENT按键对客户评级 */
        /* todo写数据 */
        if (1 == dos_sscanf(pszDTMFDigit, "%d", &lKey))
        {
            if (lKey >= 0 && lKey <= 9)
            {
                sc_ep_update_corpclients(pstSCB->ulCustomID, lKey);
                sc_logr_debug(SC_ESL, "!!!!!!!!!!!!!!!!!!!!%d, %d\n\n", pstSCB->ulCustomID, lKey);
            }
        }

        pstSCBOther = sc_scb_get(pstSCB->usOtherSCBNo);
        if (DOS_ADDR_VALID(pstSCBOther)
            && !pstSCBOther->bWaitingOtherRelase)
        {
            sc_ep_esl_execute("bridge", NULL, pstSCBOther->szUUID);
            sc_ep_esl_execute("hangup", NULL, pstSCBOther->szUUID);
        }
    }

    sc_call_trace(pstSCB, "Finished to process %s event.", esl_event_get_header(pstEvent, "Event-Name"));

    SC_TRACE_OUT();
    return DOS_SUCC;

process_fail:
    sc_call_trace(pstSCB, "Finished to process %s event. FAIL.", esl_event_get_header(pstEvent, "Event-Name"));

    SC_TRACE_OUT();
    return DOS_FAIL;

}

/**
 * 函数: U32 sc_ep_playback_stop(esl_handle_t *pstHandle, esl_event_t *pstEvent, SC_SCB_ST *pstSCB)
 * 功能: 处理放音结束事件
 * 参数:
 *      esl_handle_t *pstHandle : 发送句柄
 *      esl_event_t *pstEvent   : 时间
 *      SC_SCB_ST *pstSCB       : SCB
 * 返回值: 成功返回DOS_SUCC，否则返回DOS_FAIL
 */
U32 sc_ep_playback_stop(esl_handle_t *pstHandle, esl_event_t *pstEvent, SC_SCB_ST *pstSCB)
{
    U32           ulTaskMode = 0;
    U32           ulMainService = U32_BUTT;
    U32           ulErrCode = BS_TERM_NONE;
    S8            *pszMainService = NULL;
    S8            *pszPlayBalance = NULL;
    SC_SCB_ST     *pstOtherSCB    = NULL;

    SC_TRACE_IN(pstEvent, pstHandle, pstSCB, 0);

    if (DOS_ADDR_INVALID(pstEvent)
        || DOS_ADDR_INVALID(pstHandle)
        || DOS_ADDR_INVALID(pstSCB))
    {
        DOS_ASSERT(0);

        SC_TRACE_OUT();
        return DOS_SUCC;
    }

    sc_call_trace(pstSCB, "Start process event %s.", esl_event_get_header(pstEvent, "Event-Name"));
    pszMainService = esl_event_get_header(pstEvent, "variable_main_service");

    if (DOS_ADDR_INVALID(pszMainService)
        || dos_atoul(pszMainService, &ulMainService) < 0)
    {
        ulMainService = U32_BUTT;
    }

    /* 如果服务类型OK，就根据服务类型来处理。如果服务类型不OK，就直接挂机吧 */
    if (U32_BUTT != ulMainService)
    {
        if (!sc_call_check_service(pstSCB, ulMainService))
        {
            DOS_ASSERT(0);

            sc_logr_error(SC_ESL, "SCB %d donot have the service %d.", pstSCB->usSCBNo, ulMainService);
            ulErrCode = BS_TERM_SERV_INVALID;
            goto proc_error;
        }

        pstOtherSCB = sc_scb_get(pstSCB->usOtherSCBNo);

        switch (ulMainService)
        {
            case SC_SERV_AUTO_DIALING:

                /* 先减少播放次数，再判断播放次数，如果播放次数已经使用完就需要后续处理 */
                pstSCB->ucCurrentPlyCnt--;
                if (pstSCB->ucCurrentPlyCnt <= 0)
                {
                    ulTaskMode = sc_task_get_mode(pstSCB->usTCBNo);
                    if (ulTaskMode >= SC_TASK_MODE_BUTT)
                    {
                        DOS_ASSERT(0);
                        ulErrCode = BS_TERM_TASK_PARAM_ERR;
                        goto proc_error;
                    }

                    switch (ulTaskMode)
                    {
                        /* 以两种放音结束后需要挂断 */
                        case SC_TASK_MODE_KEY4AGENT:
                        case SC_TASK_MODE_KEY4AGENT1:
                        case SC_TASK_MODE_AUDIO_ONLY:
                            /* 如果不在呼叫队列里面，就有可能要挂断 */
                            if (!pstSCB->bIsInQueue && !SC_SCB_IS_VALID(pstOtherSCB))
                            {
                                sc_logr_notice(SC_ESL, "Hangup call for there is no input.(%s)", pstSCB->szUUID);
                                sc_ep_esl_execute("hangup", NULL, pstSCB->szUUID);
                            }
                            break;

                        /* 放音后接通坐席 */
                        case SC_TASK_MODE_AGENT_AFTER_AUDIO:
                            /* 1.获取坐席队列，2.查找坐席。3.接通坐席 */
                            sc_ep_call_queue_add(pstSCB, sc_task_get_agent_queue(pstSCB->usTCBNo));
                            break;

                        /* 这个地方出故障了 */
                        case SC_TASK_MODE_DIRECT4AGETN:
                        default:
                            DOS_ASSERT(0);
                            ulErrCode = BS_TERM_TASK_PARAM_ERR;
                            goto proc_error;
                    }
                }

                break;

            case SC_SERV_NUM_VERIFY:
                break;

            default:
                DOS_ASSERT(0);
                break;
        }
    }
    else
    {
        pszPlayBalance = esl_event_get_header(pstEvent, "play_balance");
        if (DOS_ADDR_VALID(pszPlayBalance))
        {
            /* 播放余额，不需要挂断 */
            sc_logr_debug(SC_ESL, "SCB %d play balance, %s", pstSCB->usSCBNo, pszPlayBalance);
        }
        else
        {
            sc_logr_notice(SC_ESL, "SCB %d donot needs handle any playback application.", pstSCB->usSCBNo);
            sc_ep_esl_execute("hangup", NULL, pstSCB->szUUID);
        }
    }

    sc_call_trace(pstSCB, "Finished to process %s event.", esl_event_get_header(pstEvent, "Event-Name"));

    SC_TRACE_OUT();
    return DOS_SUCC;

proc_error:

    sc_call_trace(pstSCB,"FAILED to process %s event. Call will be hangup.", esl_event_get_header(pstEvent, "Event-Name"));

    sc_ep_hangup_call(pstSCB, ulErrCode);

    return DOS_FAIL;
}


/**
 * 函数: U32 sc_ep_session_heartbeat(esl_handle_t *pstHandle, esl_event_t *pstEvent, SC_SCB_ST *pstSCB)
 * 功能: 处理ESL的CHANNEL HEARTBEAT事件
 * 参数:
 *      esl_handle_t *pstHandle : 发送句柄
 *      esl_event_t *pstEvent   : 时间
 *      SC_SCB_ST *pstSCB       : SCB
 * 返回值: 成功返回DOS_SUCC，否则返回DOS_FAIL
 */
U32 sc_ep_session_heartbeat(esl_handle_t *pstHandle, esl_event_t *pstEvent, SC_SCB_ST *pstSCB)
{
    SC_TRACE_IN(pstEvent, pstHandle, pstSCB, 0);

    if (DOS_ADDR_INVALID(pstEvent)
        || DOS_ADDR_INVALID(pstHandle)
        || DOS_ADDR_INVALID(pstSCB))
    {
        DOS_ASSERT(0);

        SC_TRACE_OUT();
        return DOS_SUCC;
    }

    sc_call_trace(pstSCB, "Start process event %s.", esl_event_get_header(pstEvent, "Event-Name"));

    sc_call_trace(pstSCB, "Finished to process %s event.", esl_event_get_header(pstEvent, "Event-Name"));

    SC_TRACE_OUT();
    return DOS_SUCC;

}

/**
 * 函数: U32 sc_ep_process(esl_handle_t *pstHandle, esl_event_t *pstEvent)
 * 功能: 分发各种ESL事件
 * 参数:
 *      esl_handle_t *pstHandle : 发送句柄
 *      esl_event_t *pstEvent   : 时间
 * 返回值: 成功返回DOS_SUCC，否则返回DOS_FAIL
 */
U32 sc_ep_process(esl_handle_t *pstHandle, esl_event_t *pstEvent)
{
    S8                     *pszUUID = NULL;
    SC_SCB_ST              *pstSCB = NULL;
    U32                    ulRet = DOS_FAIL;

    SC_TRACE_IN(pstEvent, pstHandle, 0, 0);

    if (DOS_ADDR_INVALID(pstEvent) || DOS_ADDR_INVALID(pstHandle))
    {
        DOS_ASSERT(0);

        SC_TRACE_OUT();
        return DOS_FAIL;
    }

    /* 获取事件的UUID */
    if (ESL_EVENT_BACKGROUND_JOB == pstEvent->event_id)
    {
        g_astEPMsgStat[SC_EP_STAT_PROC].ulBGJob++;
        return sc_ep_backgroud_job_proc(pstHandle, pstEvent);
    }

    pszUUID = esl_event_get_header(pstEvent, "Caller-Unique-ID");
    if (DOS_ADDR_INVALID(pszUUID))
    {
        DOS_ASSERT(0);
        return DOS_FAIL;
    }

    /* 系统统计 */
    sc_ep_system_stat(pstEvent);

    /* 如果不是CREATE消息，就需要获取SCB */
    if (ESL_EVENT_CHANNEL_CREATE != pstEvent->event_id)
    {
        pstSCB = sc_scb_hash_tables_find(pszUUID);
        if (DOS_ADDR_INVALID(pstSCB)
            || !pstSCB->bValid)
        {
            DOS_ASSERT(0);

            return DOS_FAIL;
        }
    }

    sc_logr_info(SC_ESL, "Start process event: %s(%d), SCB No:%s"
                    , esl_event_get_header(pstEvent, "Event-Name")
                    , pstEvent->event_id
                    , esl_event_get_header(pstEvent, "variable_scb_number"));

    switch (pstEvent->event_id)
    {
        /* 获取呼叫状态 */
        case ESL_EVENT_CHANNEL_PARK:
            g_astEPMsgStat[SC_EP_STAT_PROC].ulPark++;
            ulRet = sc_ep_channel_park_proc(pstHandle, pstEvent, pstSCB);
            if (ulRet != DOS_SUCC)
            {
                sc_logr_info(SC_ESL, "Hangup for process event %s fail. UUID: %s", esl_event_get_header(pstEvent, "Event-Name"), pszUUID);
            }
            break;

        case ESL_EVENT_CHANNEL_CREATE:
            g_astEPMsgStat[SC_EP_STAT_PROC].ulCreate++;
            ulRet = sc_ep_channel_create_proc(pstHandle, pstEvent);
            if (ulRet != DOS_SUCC)
            {
                sc_ep_esl_execute("hangup", NULL, pszUUID);
                sc_logr_info(SC_ESL, "Hangup for process event %s fail. UUID: %s", esl_event_get_header(pstEvent, "Event-Name"), pszUUID);
            }
            break;

        case ESL_EVENT_CHANNEL_ANSWER:
            g_astEPMsgStat[SC_EP_STAT_PROC].ulAnswer++;
            ulRet = sc_ep_channel_answer(pstHandle, pstEvent, pstSCB);
            break;

        case ESL_EVENT_CHANNEL_HANGUP:
            g_astEPMsgStat[SC_EP_STAT_PROC].ulHungup++;
            ulRet = sc_ep_channel_hungup_proc(pstHandle, pstEvent, pstSCB);
            break;

        case ESL_EVENT_CHANNEL_HANGUP_COMPLETE:
            g_astEPMsgStat[SC_EP_STAT_PROC].ulHungupCom++;
            ulRet = sc_ep_channel_hungup_complete_proc(pstHandle, pstEvent, pstSCB);
            break;

        case ESL_EVENT_DTMF:
            g_astEPMsgStat[SC_EP_STAT_PROC].ulDTMF++;
            ulRet = sc_ep_dtmf_proc(pstHandle, pstEvent, pstSCB);
            break;

        case ESL_EVENT_PLAYBACK_STOP:
            ulRet = sc_ep_playback_stop(pstHandle, pstEvent, pstSCB);
            break;

        case ESL_EVENT_SESSION_HEARTBEAT:
            ulRet = sc_ep_session_heartbeat(pstHandle, pstEvent, pstSCB);
            break;

        case ESL_EVENT_CHANNEL_HOLD:
            g_astEPMsgStat[SC_EP_STAT_PROC].ulHold++;
            ulRet = sc_ep_channel_hold(pstHandle, pstEvent, pstSCB);
            break;

        case ESL_EVENT_CHANNEL_UNHOLD:
            g_astEPMsgStat[SC_EP_STAT_PROC].ulUnhold++;
            ulRet = sc_ep_channel_hold(pstHandle, pstEvent, pstSCB);
            break;

        default:
            DOS_ASSERT(0);
            ulRet = DOS_FAIL;

            sc_logr_info(SC_ESL, "Recv unhandled event: %s(%d)", esl_event_get_header(pstEvent, "Event-Name"), pstEvent->event_id);
            break;
    }

    sc_logr_info(SC_ESL, "Process finished event: %s(%d). Result:%s"
                    , esl_event_get_header(pstEvent, "Event-Name")
                    , pstEvent->event_id
                    , (DOS_SUCC == ulRet) ? "Successfully" : "FAIL");

    SC_TRACE_OUT();
    return ulRet;
}

/**
 * 函数: VOID*sc_ep_process_runtime(VOID *ptr)
 * 功能: ESL事件处理主线程函数
 * 参数:
 * 返回值:
 */
VOID*sc_ep_process_runtime(VOID *ptr)
{
    DLL_NODE_S          *pstListNode = NULL;
    esl_event_t         *pstEvent = NULL;
    U32                 ulRet;
    struct timespec     stTimeout;
    SC_EP_TASK_CB       *pstEPTaskList = (SC_EP_TASK_CB*)ptr;

    if (DOS_ADDR_INVALID(pstEPTaskList))
    {
        DOS_ASSERT(0);
        return NULL;
    }

    for (;;)
    {
        pthread_mutex_lock(&pstEPTaskList->mutexMsgList);
        stTimeout.tv_sec = time(0) + 1;
        stTimeout.tv_nsec = 0;
        pthread_cond_timedwait(&pstEPTaskList->contMsgList, &pstEPTaskList->mutexMsgList, &stTimeout);
        pthread_mutex_unlock(&pstEPTaskList->mutexMsgList);

        while (1)
        {
            if (DLL_Count(&pstEPTaskList->stMsgList) <= 0)
            {
                break;
            }

            pthread_mutex_lock(&pstEPTaskList->mutexMsgList);
            pstListNode = dll_fetch(&pstEPTaskList->stMsgList);
            if (DOS_ADDR_INVALID(pstListNode))
            {
                DOS_ASSERT(0);

                pthread_mutex_unlock(&pstEPTaskList->mutexMsgList);
                continue;
            }

            pthread_mutex_unlock(&pstEPTaskList->mutexMsgList);

            if (DOS_ADDR_INVALID(pstListNode->pHandle))
            {
                DOS_ASSERT(0);
                continue;
            }

            pstEvent = (esl_event_t*)pstListNode->pHandle;

            pstListNode->pHandle = NULL;
            DLL_Init_Node(pstListNode)
            dos_dmem_free(pstListNode);

            sc_logr_info(SC_ESL, "ESL event process START. %s(%d), SCB No:%s, Channel Name: %s"
                            , esl_event_get_header(pstEvent, "Event-Name")
                            , pstEvent->event_id
                            , esl_event_get_header(pstEvent, "variable_scb_number")
                            , esl_event_get_header(pstEvent, "Channel-Name"));

            ulRet = sc_ep_process(&g_pstHandle->stSendHandle, pstEvent);
            if (ulRet != DOS_SUCC)
            {
                DOS_ASSERT(0);
            }

            sc_logr_info(SC_ESL, "ESL event process FINISHED. %s(%d), SCB No:%s Processed, Result: %d"
                            , esl_event_get_header(pstEvent, "Event-Name")
                            , pstEvent->event_id
                            , esl_event_get_header(pstEvent, "variable_scb_number")
                            , ulRet);

            esl_event_destroy(&pstEvent);
        }
    }

    return NULL;
}

VOID*sc_ep_process_master(VOID *ptr)
{
    DLL_NODE_S          *pstListNode = NULL;
    esl_event_t         *pstEvent = NULL;
    struct timespec     stTimeout;
    S8                  *pszUUID;
    U32                 ulSrvInd;
    S32                 i;
    static U32          ulSrvIndex = 0;
    SC_EP_TASK_CB       *pstEPTaskList = (SC_EP_TASK_CB*)ptr;
    if (DOS_ADDR_INVALID(pstEPTaskList))
    {
        DOS_ASSERT(0);
        return NULL;
    }

    for (;;)
    {
        pthread_mutex_lock(&pstEPTaskList->mutexMsgList);
        stTimeout.tv_sec = time(0) + 1;
        stTimeout.tv_nsec = 0;
        pthread_cond_timedwait(&pstEPTaskList->contMsgList, &pstEPTaskList->mutexMsgList, &stTimeout);
        pthread_mutex_unlock(&pstEPTaskList->mutexMsgList);

        while (1)
        {
            if (DLL_Count(&pstEPTaskList->stMsgList) <= 0)
            {
                break;
            }

            pthread_mutex_lock(&pstEPTaskList->mutexMsgList);

            pstListNode = dll_fetch(&pstEPTaskList->stMsgList);
            if (DOS_ADDR_INVALID(pstListNode))
            {
                DOS_ASSERT(0);

                pthread_mutex_unlock(&pstEPTaskList->mutexMsgList);
                continue;
            }

            pthread_mutex_unlock(&pstEPTaskList->mutexMsgList);

            if (DOS_ADDR_INVALID(pstListNode->pHandle))
            {
                DOS_ASSERT(0);
                dos_dmem_free(pstListNode);
                pstListNode = NULL;
                continue;
            }

            pstEvent = (esl_event_t*)pstListNode->pHandle;

            switch (pstEvent->event_id)
            {
                case ESL_EVENT_BACKGROUND_JOB:
                    g_astEPMsgStat[SC_EP_STAT_RECV].ulBGJob++;
                    break;
                /* 获取呼叫状态 */
                case ESL_EVENT_CHANNEL_PARK:
                    g_astEPMsgStat[SC_EP_STAT_RECV].ulPark++;
                    break;

                case ESL_EVENT_CHANNEL_CREATE:
                    g_astEPMsgStat[SC_EP_STAT_RECV].ulCreate++;
                    break;

                case ESL_EVENT_CHANNEL_ANSWER:
                    g_astEPMsgStat[SC_EP_STAT_RECV].ulAnswer++;
                    break;

                case ESL_EVENT_CHANNEL_HANGUP:
                    g_astEPMsgStat[SC_EP_STAT_RECV].ulHungup++;
                    break;

                case ESL_EVENT_CHANNEL_HANGUP_COMPLETE:
                    g_astEPMsgStat[SC_EP_STAT_RECV].ulHungupCom++;
                    break;

                case ESL_EVENT_DTMF:
                    g_astEPMsgStat[SC_EP_STAT_RECV].ulDTMF++;
                    break;

                case ESL_EVENT_CHANNEL_HOLD:
                    g_astEPMsgStat[SC_EP_STAT_RECV].ulHold++;
                    break;

                case ESL_EVENT_CHANNEL_UNHOLD:
                    g_astEPMsgStat[SC_EP_STAT_RECV].ulUnhold++;
                    break;

                default:
                    break;
            }


            /* 一些消息特殊处理 */
            if (ESL_EVENT_BACKGROUND_JOB == pstEvent->event_id)
            {
                sc_ep_process(&g_pstHandle->stSendHandle, pstEvent);

                pstListNode->pHandle = NULL;
                DLL_Init_Node(pstListNode)
                dos_dmem_free(pstListNode);

                esl_event_destroy(&pstEvent);

                continue;
            }

#if 1
            pszUUID = esl_event_get_header(pstEvent, "Caller-Unique-ID");
            if (DOS_ADDR_INVALID(pszUUID))
            {
                DOS_ASSERT(0);
                goto process_fail;
            }

            for (i=0; i< dos_strlen(pszUUID); i++)
            {
                ulSrvIndex += pszUUID[i];
            }

            /* 第0个位master任务，不能分配数据 */
            ulSrvInd = (ulSrvIndex % (SC_EP_TASK_NUM - 1) + 1);
#else
            if (ESL_EVENT_CHANNEL_CREATE == pstEvent->event_id)
            {
                pszUUID = esl_event_get_header(pstEvent, "Caller-Unique-ID");
                if (DOS_ADDR_INVALID(pszUUID))
                {
                    DOS_ASSERT(0);
                    goto process_fail;
                }

                ulSrvInd = (ulSrvIndex % 2 + 1);
                ulSrvIndex++;
                dos_snprintf(szBuffCmd, sizeof(szBuffCmd), "srv_index=%d", ulSrvInd);
                sc_ep_esl_execute("set", szBuffCmd, pszUUID);
            }
            else
            {
                pszSrvIndex = esl_event_get_header(pstEvent, "variable_srv_index");
                if (DOS_ADDR_INVALID(pszUUID)
                    || dos_atoul(pszSrvIndex, &ulSrvInd) < 0)
                {
                    DOS_ASSERT(0);
                    goto process_fail;
                }
            }
#endif

            if (ulSrvInd >= SC_EP_TASK_NUM
                || SC_MASTER_TASK_INDEX == ulSrvInd)
            {
                DOS_ASSERT(0);
                goto process_fail;
            }

            pthread_mutex_lock(&g_astEPTaskList[ulSrvInd].mutexMsgList);
            DLL_Add(&g_astEPTaskList[ulSrvInd].stMsgList, pstListNode);
            pthread_cond_signal(&g_astEPTaskList[ulSrvInd].contMsgList);
            pthread_mutex_unlock(&g_astEPTaskList[ulSrvInd].mutexMsgList);

            continue;
process_fail:
            pstListNode->pHandle = NULL;
            DLL_Init_Node(pstListNode)
            dos_dmem_free(pstListNode);

            esl_event_destroy(&pstEvent);
        }
    }

    return NULL;
}


/**
 * 函数: VOID* sc_ep_runtime(VOID *ptr)
 * 功能: ESL事件接收线程主函数
 * 参数:
 * 返回值:
 */
VOID* sc_ep_runtime(VOID *ptr)
{
    U32                  ulRet = ESL_FAIL;
    DLL_NODE_S           *pstDLLNode = NULL;
    // 判断第一次连接是否成功
    static BOOL bFirstConnSucc = DOS_FALSE;

    for (;;)
    {
        /* 如果退出标志被置上，就准备退出了 */
        if (g_pstHandle->blIsWaitingExit)
        {
            sc_logr_notice(SC_ESL, "%s", "Event process exit flag has been set. the task will be exit.");
            break;
        }

        /*
         * 检查连接是否正常
         * 如果连接不正常，就准备重连
         **/
        if (!g_pstHandle->blIsESLRunning)
        {
            sc_logr_notice(SC_ESL, "%s", "ELS for event connection has been down, re-connect.");
            g_pstHandle->stRecvHandle.event_lock = 1;
            ulRet = esl_connect(&g_pstHandle->stRecvHandle, "127.0.0.1", 8021, NULL, "ClueCon");
            if (ESL_SUCCESS != ulRet)
            {
                esl_disconnect(&g_pstHandle->stRecvHandle);
                sc_logr_notice(SC_ESL, "ELS for event re-connect fail, return code:%d, Msg:%s. Will be retry after 1 second.", ulRet, g_pstHandle->stRecvHandle.err);

                sleep(1);
                continue;
            }

            g_pstHandle->blIsESLRunning = DOS_TRUE;
            g_pstHandle->ulESLDebugLevel = ESL_LOG_LEVEL_INFO;
            esl_global_set_default_logger(g_pstHandle->ulESLDebugLevel);
            esl_events(&g_pstHandle->stRecvHandle, ESL_EVENT_TYPE_PLAIN, SC_EP_EVENT_LIST);

            sc_logr_notice(SC_ESL, "%s", "ELS for event connect Back to Normal.");
        }

        if (!bFirstConnSucc)
        {
            bFirstConnSucc = DOS_TRUE;
            sc_ep_esl_execute_cmd("api reloadxml\r\n");
        }

        ulRet = esl_recv_event(&g_pstHandle->stRecvHandle, 1, NULL);
        if (ESL_FAIL == ulRet)
        {
            DOS_ASSERT(0);

            sc_logr_info(SC_ESL, "%s", "ESL Recv event fail, continue.");
            g_pstHandle->blIsESLRunning = DOS_FALSE;
            continue;
        }

        esl_event_t *pstEvent = g_pstHandle->stRecvHandle.last_ievent;
        if (DOS_ADDR_INVALID(pstEvent))
        {
            DOS_ASSERT(0);

            sc_logr_info(SC_ESL, "%s", "ESL get event fail, continue.");
            g_pstHandle->blIsESLRunning = DOS_FALSE;
            continue;
        }
#if 0
        S8                   *pszIsLoopbackLeg = NULL;
        S8                   *pszIsAutoCall = NULL;

        /* 如果是AUTO Call, 需要吧loopback call的leg a丢掉 */
        pszIsLoopbackLeg = esl_event_get_header(pstEvent, "variable_loopback_leg");
        pszIsAutoCall = esl_event_get_header(pstEvent, "variable_auto_call");
        if (pszIsLoopbackLeg && pszIsAutoCall
            && 0 == dos_strnicmp(pszIsLoopbackLeg, "A", dos_strlen("A"))
            && 0 == dos_strnicmp(pszIsAutoCall, "true", dos_strlen("true")))
        {
            sc_logr_info(SC_ESL, "%s", "ESL drop loopback call leg A.");
            continue;
        }
#endif

        sc_logr_info(SC_ESL, "ESL recv thread recv event %s(%d)."
                        , esl_event_get_header(pstEvent, "Event-Name")
                        , pstEvent->event_id);

        pstDLLNode = (DLL_NODE_S *)dos_dmem_alloc(sizeof(DLL_NODE_S));
        if (DOS_ADDR_INVALID(pstDLLNode))
        {
            DOS_ASSERT(0);

            sc_logr_info(SC_ESL, "ESL recv thread recv event %s(%d). Alloc memory fail. Drop"
                            , esl_event_get_header(pstEvent, "Event-Name")
                            , pstEvent->event_id);

            continue;
        }

        pthread_mutex_lock(&g_astEPTaskList[SC_MASTER_TASK_INDEX].mutexMsgList);
        DLL_Init_Node(pstDLLNode);
        pstDLLNode->pHandle = NULL;
        esl_event_dup((esl_event_t **)(&pstDLLNode->pHandle), pstEvent);
        DLL_Add(&g_astEPTaskList[SC_MASTER_TASK_INDEX].stMsgList, pstDLLNode);

        pthread_cond_signal(&g_astEPTaskList[SC_MASTER_TASK_INDEX].contMsgList);
        pthread_mutex_unlock(&g_astEPTaskList[SC_MASTER_TASK_INDEX].mutexMsgList);
    }

    /* @TODO 释放资源 */
    return NULL;
}

/* 初始化事件处理模块 */
U32 sc_ep_init()
{
    S32 i;
    SC_TRACE_IN(0, 0, 0, 0);

    g_pstHandle = dos_dmem_alloc(sizeof(SC_EP_HANDLE_ST));
    g_pstHashGWGrp = hash_create_table(SC_GW_GRP_HASH_SIZE, NULL);
    g_pstHashGW = hash_create_table(SC_GW_HASH_SIZE, NULL);
    g_pstHashDIDNum = hash_create_table(SC_IP_DID_HASH_SIZE, NULL);
    g_pstHashSIPUserID = hash_create_table(SC_IP_USERID_HASH_SIZE, NULL);
    g_pstHashBlackList = hash_create_table(SC_BLACK_LIST_HASH_SIZE, NULL);
    g_pstHashTTNumber = hash_create_table(SC_TT_NUMBER_HASH_SIZE, NULL);
    g_pstHashCaller = hash_create_table(SC_CALLER_HASH_SIZE, NULL);
    g_pstHashCallerGrp = hash_create_table(SC_CALLER_GRP_HASH_SIZE, NULL);
    g_pstHashNumberlmt = hash_create_table(SC_NUMBER_LMT_HASH_SIZE, NULL);
    g_pstHashCallerSetting = hash_create_table(SC_CALLER_SETTING_HASH_SIZE, NULL);
    if (DOS_ADDR_INVALID(g_pstHandle)
        || DOS_ADDR_INVALID(g_pstHashGW)
        || DOS_ADDR_INVALID(g_pstHashGWGrp)
        || DOS_ADDR_INVALID(g_pstHashDIDNum)
        || DOS_ADDR_INVALID(g_pstHashSIPUserID)
        || DOS_ADDR_INVALID(g_pstHashBlackList)
        || DOS_ADDR_INVALID(g_pstHashCaller)
        || DOS_ADDR_INVALID(g_pstHashCallerGrp)
        || DOS_ADDR_INVALID(g_pstHashTTNumber)
        || DOS_ADDR_INVALID(g_pstHashNumberlmt))
    {
        DOS_ASSERT(0);

        goto init_fail;
    }


    for (i = 0; i < SC_EP_TASK_NUM; i++)
    {
        pthread_mutex_init(&g_astEPTaskList[i].mutexMsgList, NULL);
        pthread_cond_init(&g_astEPTaskList[i].contMsgList, NULL);
        DLL_Init(&g_astEPTaskList[i].stMsgList);
    }

    dos_memzero(g_pstHandle, sizeof(SC_EP_HANDLE_ST));
    g_pstHandle->blIsESLRunning = DOS_FALSE;
    g_pstHandle->blIsWaitingExit = DOS_FALSE;

    dos_memzero(g_astEPMsgStat, sizeof(g_astEPMsgStat));

    DLL_Init(&g_stEventList)
    DLL_Init(&g_stRouteList);
    DLL_Init(&g_stNumTransformList);
    DLL_Init(&g_stCustomerList);

    /* 以下三项加载顺序不能更改 */
    sc_load_gateway(SC_INVALID_INDEX);
    sc_load_gateway_grp(SC_INVALID_INDEX);
    sc_load_gw_relationship();

    sc_load_route(SC_INVALID_INDEX);
    sc_load_num_transform(SC_INVALID_INDEX);
    sc_load_customer(SC_INVALID_INDEX);
    sc_load_did_number(SC_INVALID_INDEX);
    sc_load_sip_userid(SC_INVALID_INDEX);
    sc_load_black_list(SC_INVALID_INDEX);
    sc_load_tt_number(SC_INVALID_INDEX);

    /* 以下三项的加载顺序同样不可乱,同时必须保证之前已经加载DID号码(号码组业务逻辑) */
    sc_load_caller(SC_INVALID_INDEX);
    sc_load_caller_grp(SC_INVALID_INDEX);
    sc_load_number_lmt(SC_INVALID_INDEX);
    sc_load_caller_relationship();

    sc_load_caller_setting(SC_INVALID_INDEX);
    SC_TRACE_OUT();
    return DOS_SUCC;
init_fail:

    return DOS_FAIL;
}

/* 启动事件处理模块 */
U32 sc_ep_start()
{
    S32 i;

    SC_TRACE_IN(0, 0, 0, 0);

    for (i=0; i<SC_EP_TASK_NUM; i++)
    {
        if (SC_MASTER_TASK_INDEX == i)
        {
            if (pthread_create(&g_astEPTaskList[i].pthTaskID, NULL, sc_ep_process_master, &g_astEPTaskList[i]) < 0)
            {
                SC_TRACE_OUT();
                return DOS_FAIL;
            }
        }
        else
        {
            if (pthread_create(&g_astEPTaskList[i].pthTaskID, NULL, sc_ep_process_runtime, &g_astEPTaskList[i]) < 0)
            {
                SC_TRACE_OUT();
                return DOS_FAIL;
            }
        }
    }

    if (pthread_create(&g_pstHandle->pthID, NULL, sc_ep_runtime, NULL) < 0)
    {
        SC_TRACE_OUT();
        return DOS_FAIL;
    }

    SC_TRACE_OUT();
    return DOS_SUCC;
}

/* 停止事件处理模块 */
U32 sc_ep_shutdown()
{
    SC_TRACE_IN(0, 0, 0, 0);

    g_pstHandle->blIsWaitingExit = DOS_TRUE;

    SC_TRACE_OUT();
    return DOS_SUCC;
}

#ifdef __cplusplus
}
#endif /* __cplusplus */



