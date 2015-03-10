/*
 *            (C) Copyright 2014, DIPCC . Co., Ltd.
 *                    ALL RIGHTS RESERVED
 *
 *  文件名: sc_api_process.c
 *
 *  创建时间: 2014年12月16日10:18:20
 *  作    者: Larry
 *  描    述: 处理HTTP请求，并将HTTP请求中所携带的命令分发到各个处理模块
 *  修改历史:
 */

#ifdef __cplusplus
extern "C"{
#endif /* __cplusplus */


/* include public header files */
#include <dos.h>
#include <pthread.h>
#include <semaphore.h>


/* include private header files */
#include "sc_def.h"
#include "sc_debug.h"
#include "sc_httpd_def.h"
#include "sc_http_api.h"
#include "sc_acd_def.h"

U32 sc_http_api_reload_xml(SC_HTTP_CLIENT_CB_S *pstClient);
U32 sc_http_api_task_ctrl(SC_HTTP_CLIENT_CB_S *pstClient);
U32 sc_http_api_num_verify(SC_HTTP_CLIENT_CB_S *pstClient);
U32 sc_http_api_call_ctrl(SC_HTTP_CLIENT_CB_S *pstClient);
U32 sc_http_api_agent_action(SC_HTTP_CLIENT_CB_S *pstClient);
U32 sc_acd_http_req_proc(U32 ulAction, U32 ulAgentID, S8 *pszUserID);

/* global parameters */
SC_HTTP_REQ_REG_TABLE_SC g_pstHttpReqRegTable[] =
{
    {"reload",                   sc_http_api_reload_xml},
    {"task",                     sc_http_api_task_ctrl},
    {"verify",                   sc_http_api_num_verify},
    {"callctrl",                 sc_http_api_call_ctrl},
    {"agent",                    sc_http_api_agent_action},

    {"",                         NULL}
};


/* declare functions */
/**
 * 函数: S8 *sc_http_api_get_value(list_t *pstParamList, S8 *pszKey)
 * 功能:
 *      从http请求控制块pstParamList中获取相应字段pszKey的值
 * 参数:
 *      list_t *pstParamList: 参数列表
 *      S8 *pszKey: 参数名
 * 返回值: 如果找到相关参数，就返回该参数的首指针，否则返回NULL
 */
static S8 *sc_http_api_get_value(list_t *pstParamList, S8 *pszKey)
{
    SC_API_PARAMS_ST *pstParamListNode = NULL;
    list_t           *pstLineNode = pstParamList;

    SC_TRACE_IN((U64)pstParamList, (U64)pszKey, 0, 0);

    if (!pstParamList || !pszKey)
    {
        DOS_ASSERT(0);
        SC_TRACE_OUT();
        return NULL;
    }

    while (1)
    {
        if (dos_list_is_empty(pstParamList))
        {
            break;
        }

        pstLineNode = dos_list_work(pstParamList, pstLineNode);
        if (!pstLineNode)
        {
            break;
        }

        pstParamListNode = dos_list_entry(pstLineNode, SC_API_PARAMS_ST, stList);

        if (pstParamListNode
            && 0 == dos_strcmp(pstParamListNode->pszStringName, pszKey))
        {
            SC_TRACE_OUT();
            return pstParamListNode->pszStringVal;
        }
    }

    SC_TRACE_OUT();
    return NULL;
}

U32 sc_http_api_reload_xml(SC_HTTP_CLIENT_CB_S *pstClient)
{
    U32 ulCustomID, ulAction, ulGatewayID, ulDialplan;
    S8  *pszCustomID, *pszAction, *pszGatewayID, *pszDialplan;;

    pszCustomID = sc_http_api_get_value(&pstClient->stParamList, "userid");
    if (!pszCustomID)
    {
        ulCustomID = U32_BUTT;

        goto invalid_params;
    }
    else
    {
        if (dos_atoul(pszCustomID, &ulCustomID) < 0)
        {
            ulCustomID = U32_BUTT;
        }
    }

    pszAction = sc_http_api_get_value(&pstClient->stParamList, "action");
    if (!pszAction)
    {
        ulAction = SC_API_CMD_ACTION_BUTT;
    }
    else
    {
        ulAction = SC_API_CMD_ACTION_BUTT;

        if (0 == dos_strncmp(pszAction, "pause", dos_strlen("pause")))
        {
            ulAction = SC_API_CMD_ACTION_PAUSE;
        }
        else if (0 == dos_strncmp(pszAction, "monitoring", dos_strlen("monitoring")))
        {
            ulAction = SC_API_CMD_ACTION_MONITORING;
        }
        else if (0 == dos_strncmp(pszAction, "hungup", dos_strlen("hungup")))
        {
            ulAction = SC_API_CMD_ACTION_HUNGUP;
        }
    }

    pszGatewayID = sc_http_api_get_value(&pstClient->stParamList, "gateway");
    if (!pszGatewayID)
    {
        ulGatewayID = U32_BUTT;
    }
    else
    {
        if (dos_atoul(pszGatewayID, &ulGatewayID) < 0)
        {
            ulGatewayID = U32_BUTT;
        }
    }

    pszDialplan = sc_http_api_get_value(&pstClient->stParamList, "dialplan");
    if (!pszDialplan)
    {
        ulDialplan = U32_BUTT;
    }
    else
    {
        if (dos_atoul(pszDialplan, &ulDialplan) < 0)
        {
            ulDialplan = U32_BUTT;
        }
    }

    return DOS_SUCC;

invalid_params:
    pstClient->ulResponseCode = 200;
    pstClient->ulErrCode = SC_HTTP_ERRNO_INVALID_PARAM;

    SC_TRACE_OUT();
    return DOS_FAIL;

}

U32 sc_http_api_task_ctrl(SC_HTTP_CLIENT_CB_S *pstClient)
{
    S8 *pszCustomID;
    S8 *pszTaskID;
    S8 *pszAction;
    U32 ulCustomID, ulTaskID, ulAction;
    SC_TASK_CTRL_CMD_ST *pstCMD = NULL;

    if (DOS_ADDR_INVALID(pstClient))
    {
        DOS_ASSERT(0);

        return SC_HTTP_ERRNO_INVALID_REQUEST;
    }

    SC_TRACE_IN(pstClient, 0, 0, 0);
/*
    pszCMD = sc_http_api_get_value(&pstClient->stParamList, "cmd");
    if (!pszCMD || '\0' == pszCMD[0])
    {
        DOS_ASSERT(0);
        goto invalid_params;
    }
    if (dos_strcmp(pszCMD, "task") != 0)
    {
        DOS_ASSERT(0);
        goto invalid_params;
    }
*/

    pszCustomID = sc_http_api_get_value(&pstClient->stParamList, "userid");
    if (!pszCustomID || '\0' == pszCustomID[0])
    {
        DOS_ASSERT(0);
        goto invalid_params;
    }
    if (dos_atoul(pszCustomID, &ulCustomID) < 0)
    {
        DOS_ASSERT(0);
        goto invalid_params;
    }

    pszTaskID = sc_http_api_get_value(&pstClient->stParamList, "task");
    if (!pszTaskID || '\0' == pszTaskID[0])
    {
        DOS_ASSERT(0);
        goto invalid_params;
    }
    if (dos_atoul(pszTaskID, &ulTaskID) < 0)
    {
        DOS_ASSERT(0);
        goto invalid_params;
    }

    pszAction = sc_http_api_get_value(&pstClient->stParamList, "action");
    if (!pszAction || '\0' == pszAction[0])
    {
        DOS_ASSERT(0);
        goto invalid_params;
    }
    if (dos_strcmp(pszAction, "start") == 0)
    {
        ulAction = SC_API_CMD_ACTION_START;
    }
    else if (dos_strcmp(pszAction, "stop") == 0)
    {
        ulAction = SC_API_CMD_ACTION_STOP;
    }
    else if (dos_strcmp(pszAction, "pause") == 0)
    {
        ulAction = SC_API_CMD_ACTION_PAUSE;
    }
    else if (dos_strcmp(pszAction, "continue") == 0)
    {
        ulAction = SC_API_CMD_ACTION_CONTINUE;
    }
    else
    {
        DOS_ASSERT(0);
        goto invalid_params;
    }

    /* 开始发送命令 */
    pstCMD = (SC_TASK_CTRL_CMD_ST *)dos_dmem_alloc(sizeof(SC_TASK_CTRL_CMD_ST));
    if (!pstCMD)
    {
        DOS_ASSERT(0);

        goto exec_fail;
    }
    dos_memzero(pstCMD, sizeof(SC_TASK_CTRL_CMD_ST));

    pstCMD->ulCustomID = ulCustomID;
    pstCMD->ulTaskID = ulTaskID;
    pstCMD->ulCMD = SC_API_CMD_TASK_CTRL;
    pstCMD->ulAction = ulAction;
    pstCMD->ulCMDErrCode = U32_BUTT;
    sem_init(&pstCMD->semCMDExecNotify, 0, 0);

    if (sc_task_cmd_queue_add(pstCMD) != DOS_SUCC)
    {
        DOS_ASSERT(0);

        goto exec_fail;
    }

    /* TODO: 需要设置等待时间，如果超时，就不要再等待了 */
    sem_wait(&pstCMD->semCMDExecNotify);
#if 0
    if (U32_BUTT == pstCMD->ulCMDErrCode)
    {
        DOS_ASSERT(0);

        goto exec_fail;
    }
#endif
    pstClient->ulResponseCode = 200;
    pstClient->ulErrCode = pstCMD->ulCMDErrCode;

    if (sc_task_cmd_queue_del(pstCMD) == DOS_SUCC)
    {
        if (pstCMD->pszErrMSG)
        {
            dos_dmem_free(pstCMD->pszErrMSG);
        }
        pstCMD->pszErrMSG = NULL;

        dos_dmem_free(pstCMD);
        pstCMD = NULL;
    }
    else
    {
        pstClient->ulResponseCode = 200;
        pstClient->ulErrCode = SC_HTTP_ERRNO_CMD_EXEC_FAIL;
        DOS_ASSERT(0);
    }

    SC_TRACE_OUT();
    return DOS_TRUE;

invalid_params:
    pstClient->ulResponseCode = 200;
    pstClient->ulErrCode = SC_HTTP_ERRNO_INVALID_PARAM;

    SC_TRACE_OUT();
    return DOS_FAIL;

exec_fail:
    pstClient->ulResponseCode = 200;
    pstClient->ulErrCode = SC_HTTP_ERRNO_CMD_EXEC_FAIL;

    if (pstCMD->pszErrMSG)
    {
        dos_dmem_free(pstCMD->pszErrMSG);
    }
    pstCMD->pszErrMSG = NULL;

    dos_dmem_free(pstCMD);
    pstCMD = NULL;

    SC_TRACE_OUT();
    return DOS_FAIL;
}

U32 sc_http_api_num_verify(SC_HTTP_CLIENT_CB_S *pstClient)
{
    return DOS_FAIL;
}

U32 sc_http_api_call_ctrl(SC_HTTP_CLIENT_CB_S *pstClient)
{
    return DOS_FAIL;
}

U32 sc_http_api_agent_action(SC_HTTP_CLIENT_CB_S *pstClient)
{
    S8 *pszAgentID    = NULL;
    S8 *pszUserID     = NULL;
    S8 *pszAction     = NULL;
    U32 ulAgentID = U32_BUTT, ulAction = U32_BUTT;

    if (DOS_ADDR_INVALID(pstClient))
    {
        DOS_ASSERT(0);

        return SC_HTTP_ERRNO_INVALID_REQUEST;
    }

    SC_TRACE_IN(pstClient, 0, 0, 0);


    pszAgentID = sc_http_api_get_value(&pstClient->stParamList, "agent_id");
    pszUserID = sc_http_api_get_value(&pstClient->stParamList, "sip_userid");
    pszAction = sc_http_api_get_value(&pstClient->stParamList, "action");
    if (DOS_ADDR_INVALID(pszAgentID)
        || DOS_ADDR_INVALID(pszUserID)
        || DOS_ADDR_INVALID(pszAction))
    {
        DOS_ASSERT(0);
        return SC_HTTP_ERRNO_INVALID_REQUEST;
    }

    if (dos_atoul(pszAgentID, &ulAgentID) < 0)
    {
        DOS_ASSERT(0);
        return SC_HTTP_ERRNO_INVALID_REQUEST;
    }

    if (dos_strncmp(pszAction, "signin", sizeof("signin")) == 0)
    {
        ulAction = SC_ACD_SITE_ACTION_SIGNIN;
    }
    else if (dos_strncmp(pszAction, "signout", sizeof("signout")) == 0)
    {
        ulAction = SC_ACD_SITE_ACTION_SIGNOUT;
    }
    else if (dos_strncmp(pszAction, "login", sizeof("login")) == 0)
    {
        ulAction = SC_ACD_SITE_ACTION_ONLINE;
    }
    else if (dos_strncmp(pszAction, "logout",sizeof("logout")) == 0)
    {
        ulAction = SC_ACD_SITE_ACTION_OFFLINE;
    }
    else if (dos_strncmp(pszAction, "update",sizeof("update")) == 0)
    {
        ulAction = SC_ACD_SITE_ACTION_UPDATE;
    }
    else if (dos_strncmp(pszAction, "add",sizeof("add")) == 0)
    {
        ulAction = SC_ACD_SITE_ACTION_ADD;
    }
    else if (dos_strncmp(pszAction, "delete",sizeof("delete")) == 0)
    {
        ulAction = SC_ACD_SITE_ACTION_DELETE;
    }
    else
    {
        DOS_ASSERT(0);
        return SC_HTTP_ERRNO_INVALID_REQUEST;
    }

    if (sc_acd_http_req_proc(ulAction, ulAgentID, pszUserID) != DOS_SUCC)
    {
        DOS_ASSERT(0);
        return SC_HTTP_ERRNO_CMD_EXEC_FAIL;
    }

    return SC_HTTP_ERRNO_SUCC;
}

U32 sc_http_api_process(SC_HTTP_CLIENT_CB_S *pstClient)
{
    S8        *pStart = NULL, *pEnd = NULL;
    S8        *pszKeyWord[SC_API_PARAMS_MAX_NUM] = { 0 };
    S8        *pWord = NULL, *pValue = NULL;
    S8        szReqBuffer[SC_HTTP_REQ_MAX_LEN] = { 0 };
    S8        szReqLine[1024] = { 0 };
    S32       lKeyCnt = 0, lParamIndex = 0;
    U32       ulIndex, ulRet;
    list_t    *pstParamListNode;
    SC_API_PARAMS_ST *pstParamsList;

    SC_TRACE_IN((U64)pstClient,0,0,0);

    if (!pstClient)
    {
        DOS_ASSERT(0);
        goto cmd_prase_fail1;
    }

    if (!pstClient->ulValid)
    {
        DOS_ASSERT(0);
        goto cmd_prase_fail1;
    }

    if (!pstClient->stDataBuff.ulLength || !pstClient->stDataBuff.pszBuff)
    {
        DOS_ASSERT(0);
        goto cmd_prase_fail1;
    }

    sc_logr_debug(SC_HTTP_API, "HTTP Request: %s", pstClient->stDataBuff.pszBuff);

    /* 获取请求的文件 */
    pStart = dos_strstr(pstClient->stDataBuff.pszBuff, "GET /");
    if (!pStart)
    {
        DOS_ASSERT(0);
        goto cmd_prase_fail1;
    }
    pStart += dos_strlen("GET /");
    pEnd = dos_strstr(pStart, "?");
    if (!pStart || !pEnd
        || pEnd <= pStart)
    {
        DOS_ASSERT(0);
        goto cmd_prase_fail1;
    }
    dos_strncpy(szReqBuffer, pStart, pEnd-pStart);
    szReqBuffer[pEnd - pStart] = '\0';
    if ('\0' == szReqBuffer[0])
    {
        DOS_ASSERT(0);
        goto cmd_prase_fail1;
    }

    /* 获取请求行参数 */
    pStart = dos_strstr(pstClient->stDataBuff.pszBuff, "?");
    pEnd = dos_strstr(pstClient->stDataBuff.pszBuff, " HTTP/1.");
    if (!pStart || !pEnd)
    {
        DOS_ASSERT(0);
        goto cmd_prase_fail1;
    }

    /* 获取请求参数 */
    pStart += dos_strlen("?");
    if (pEnd - pStart >= sizeof(szReqLine))
    {
        DOS_ASSERT(0);
        goto cmd_prase_fail1;
    }
    dos_strncpy(szReqLine, pStart, pEnd - pStart);
    szReqLine[pEnd - pStart] = '\0';

    sc_logr_debug(SC_HTTP_API, "HTTP Request Line: %s?%s", szReqBuffer, szReqLine);

    /* 获取 key=value 字符串 */
    lKeyCnt = 0;
    pWord = strtok(szReqLine, "&");
    while (pWord)
    {
        pszKeyWord[lKeyCnt] = dos_dmem_alloc(dos_strlen(pWord) + 1);
        if (!pszKeyWord[lKeyCnt])
        {
            logr_warning("%s", "Alloc fail.");
            break;
        }

        dos_strcpy(pszKeyWord[lKeyCnt], pWord);
        lKeyCnt++;
        pWord = strtok(NULL, "&");
        if (NULL == pWord)
        {
            break;
        }
    }

    if (lKeyCnt<= 0)
    {
        DOS_ASSERT(0);
        goto cmd_prase_fail1;
    }

    sc_logr_debug(SC_HTTP_API, "%s", "Start prase the http request.");

    /* 解析key=value，并将结果存入链表 */
    dos_list_init(&pstClient->stParamList);
    for (lParamIndex=0; lParamIndex<lKeyCnt; lParamIndex++)
    {
        if(!pszKeyWord[lParamIndex])
        {
            continue;
        }

        sc_logr_debug(SC_HTTP_API, "Process Token: %s", pszKeyWord[lParamIndex]);

        pWord = dos_strstr(pszKeyWord[lParamIndex], "=");
        pValue = pWord;
        if (!pValue)
        {
            continue;
        }
        pValue++;
        if (!pValue)
        {
            continue;
        }
        *pWord = '\0';

        pstParamsList = (SC_API_PARAMS_ST *)dos_dmem_alloc(sizeof(SC_API_PARAMS_ST));
        if (!pstParamsList)
        {
            DOS_ASSERT(0);
            continue;
        }

        /*
         * pValue 指向的是 pszKeyWord[lParamIndex]所在地址段的某个地址，
         * 而pszKeyWord[lParamIndex]是动态申请的内存，所以这里就不用重新申请内存了
         */
        pstParamsList->pszStringName = pszKeyWord[lParamIndex];
        pstParamsList->pszStringVal = pValue;

        dos_list_add_tail(&(pstClient->stParamList), &pstParamsList->stList);
    }

    sc_logr_debug(SC_HTTP_API, "%s", "Prase the http request finished.");

    ulRet = DOS_FAIL;
    for (ulIndex=0; ulIndex<(sizeof(g_pstHttpReqRegTable)/sizeof(SC_HTTP_REQ_REG_TABLE_SC)); ulIndex++)
    {
        if (g_pstHttpReqRegTable[ulIndex].pszRequest
            && dos_strcmp(g_pstHttpReqRegTable[ulIndex].pszRequest, szReqBuffer) == 0)
        {
            if (g_pstHttpReqRegTable[ulIndex].callback)
            {
                ulRet = g_pstHttpReqRegTable[ulIndex].callback(pstClient);
            }

            break;
        }
    }

    sc_logr_notice(SC_HTTP_API, "HTTP Request process finished. Return code: %d", ulRet);

    while (1)
    {
        if (dos_list_is_empty(&pstClient->stParamList))
        {
            break;
        }

        pstParamListNode = dos_list_fetch(&pstClient->stParamList);
        if (!pstParamListNode)
        {
            break;
        }

        pstParamsList = dos_list_entry(pstParamListNode, SC_API_PARAMS_ST, stList);
        if (!pstParamsList)
        {
            continue;
        }

        pstParamsList->pszStringName = NULL;
        pstParamsList->pszStringVal = NULL;
        dos_dmem_free(pstParamsList);
        pstParamsList = NULL;
    }

    for (lParamIndex=0; lParamIndex<lKeyCnt; lParamIndex++)
    {
        dos_dmem_free(pszKeyWord[lParamIndex]);
        pszKeyWord[lParamIndex] = NULL;
    }

    if (DOS_SUCC == ulRet)
    {
        pstClient->ulErrCode = SC_HTTP_ERRNO_SUCC;
        pstClient->ulResponseCode = SC_HTTP_OK;
        SC_TRACE_OUT();
        return DOS_SUCC;
    }

cmd_prase_fail1:
    pstClient->ulErrCode = SC_HTTP_ERRNO_INVALID_REQUEST;
    pstClient->ulResponseCode = SC_HTTP_OK;
    SC_TRACE_OUT();
    return DOS_FAIL;
}

#ifdef __cplusplus
}
#endif /* __cplusplus */

