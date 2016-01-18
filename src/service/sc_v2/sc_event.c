/**
 * @file : sc_event.c
 *
 *            (C) Copyright 2014, DIPCC . Co., Ltd.
 *                    ALL RIGHTS RESERVED
 *
 * ҵ����Ʋ���Ҫ��ں�������
 *
 * @date: 2016��1��9��
 * @arthur: Larry
 */

#ifdef __cplusplus
extern "C" {
#endif /* End of __cplusplus */

#include <dos.h>
#include "sc_pub.h"
#include "sc_def.h"
#include "sc_debug.h"

/** �¼������߳̾�� */
pthread_t       g_pthEventQueue;

/** ҵ���Ӳ��ϱ�ʱ����Ϣ���� */
DLL_S           g_stEventQueue;

/** ҵ���Ӳ��ϱ�ʱ����Ϣ������ */
pthread_mutex_t g_mutexEventQueue = PTHREAD_MUTEX_INITIALIZER;

/** ҵ���Ӳ��ϱ�ʱ����Ϣ������������ */
pthread_cond_t  g_condEventQueue = PTHREAD_COND_INITIALIZER;

/** ϵͳ�Ƿ���ά��״̬, ������ҵ���Ӳ���ϴη���startup��Ϣ���ñ�ǽ�����ΪFALSE�����ɿ�չҵ�� */
BOOL            g_blInMaintain        = DOS_TRUE;

/** ҵ����ƿ�ָ�� */
SC_SRV_CB       *g_pstSCBList         = NULL;

/** ҵ����ƿ��� */
pthread_mutex_t g_mutexSCBList = PTHREAD_MUTEX_INITIALIZER;

/**
 * ��������ҵ��״̬������
 *
 * @param SC_MSG_HEAD_ST *pstMsg ��ǰ�¼�
 * @param SC_SRV_CB *pstSCB      ��ǰҵ����ƿ�
 * @param SC_SRV_INFO_ST *pstSubServ ��������ҵ����ƿ�
 *
 * return �ɹ�����DOS_SUCC�����򷵻�DOS_FALSE
 */
U32 sc_srv_call_proc(SC_MSG_TAG_ST *pstMsg, SC_SRV_CB *pstSCB, SC_SCB_TAG_ST *pstSubServ)
{
    U32 ulRet = DOS_SUCC;

    if (DOS_ADDR_INVALID(pstMsg) || DOS_ADDR_INVALID(pstSCB) || DOS_ADDR_INVALID(pstSubServ))
    {
        DOS_ASSERT(0);

        return DOS_FAIL;
    }

    sc_log(LOG_LEVEL_DEBUG, "Processing event. service:%u type: %u, scb:%u len: %u"
                , pstSubServ->usSrvType, pstMsg->ulMsgType
                , pstSCB->ulSCBNo, pstMsg->usMsgLen);

    switch (pstMsg->ulMsgType)
    {
        case SC_EVT_AUTH_RESULT:
            ulRet = sc_call_auth_rsp(pstMsg, pstSCB);
            break;

        case SC_EVT_CALL_SETUP:
            ulRet = sc_call_setup(pstMsg, pstSCB);
            break;

        case SC_EVT_EXCHANGE_MEDIA:
            ulRet = sc_call_exchange_media(pstMsg, pstSCB);
            break;

        case SC_EVT_CALL_AMSWERED:
            ulRet = sc_call_answer(pstMsg, pstSCB);
            break;

        case SC_EVT_CALL_RINGING:
            ulRet = sc_call_ringing(pstMsg, pstSCB);
            break;

        case SC_EVT_BRIDGE_START:
            ulRet = sc_call_bridge(pstMsg, pstSCB);
            break;

        case SC_EVT_BRIDGE_STOP:
            ulRet = sc_call_unbridge(pstMsg, pstSCB);
            break;

        case SC_EVT_HOLD:
            ulRet = sc_call_hold(pstMsg, pstSCB);
            break;

        case SC_EVT_CALL_RERLEASE:
            ulRet = sc_call_release(pstMsg, pstSCB);
            break;

        case SC_EVT_CALL_STATUS:
            /* ��ʱ������ */
            break;

        case SC_EVT_DTMF:
            ulRet = sc_call_dtmf(pstMsg, pstSCB);
            break;

        case SC_EVT_RECORD_START:
            /* ��ʱ������ */
            break;

        case SC_EVT_RECORD_END:
            ulRet = sc_call_record_stop(pstMsg, pstSCB);
            break;

        case SC_EVT_PLAYBACK_START:
            /* ��ʱ������ */
            break;

        case SC_EVT_PLAYBACK_END:
            ulRet = sc_call_playback_stop(pstMsg, pstSCB);
            break;

        case SC_EVT_ERROR_PORT:
            break;

        default:
            sc_log(LOG_LEVEL_NOTIC, "Invalid event type. %u", pstMsg->ulMsgType);
            break;
    }

    sc_log(LOG_LEVEL_DEBUG, "Processed event. service:%u, type: %u, len: %u. Ret: %s"
                    , pstSubServ->usSrvType, pstMsg->ulMsgType
                    , pstMsg->usMsgLen, (DOS_SUCC == ulRet) ? "succ" : "FAIL");

    return ulRet;
}

/**
 * ��������ҵ��״̬������
 *
 * @param SC_MSG_HEAD_ST *pstMsg ��ǰ�¼�
 * @param SC_SRV_CB *pstSCB      ��ǰҵ����ƿ�
 * @param SC_SRV_INFO_ST *pstSubServ ��������ҵ����ƿ�
 *
 * return �ɹ�����DOS_SUCC�����򷵻�DOS_FALSE
 */
U32 sc_srv_preview_dial_proc(SC_MSG_TAG_ST *pstMsg, SC_SRV_CB *pstSCB, SC_SCB_TAG_ST *pstSubServ)
{
    return DOS_SUCC;
}

/**
 * ��������ҵ��״̬������
 *
 * @param SC_MSG_HEAD_ST *pstMsg ��ǰ�¼�
 * @param SC_SRV_CB *pstSCB      ��ǰҵ����ƿ�
 * @param SC_SRV_INFO_ST *pstSubServ ��������ҵ����ƿ�
 *
 * return �ɹ�����DOS_SUCC�����򷵻�DOS_FALSE
 */
U32 sc_srv_auto_dial_proc(SC_MSG_TAG_ST *pstMsg, SC_SRV_CB *pstSCB, SC_SCB_TAG_ST *pstSubServ)
{
    return DOS_SUCC;
}

/**
 * ��������ҵ��״̬������
 *
 * @param SC_MSG_HEAD_ST *pstMsg ��ǰ�¼�
 * @param SC_SRV_CB *pstSCB      ��ǰҵ����ƿ�
 * @param SC_SRV_INFO_ST *pstSubServ ��������ҵ����ƿ�
 *
 * return �ɹ�����DOS_SUCC�����򷵻�DOS_FALSE
 */
U32 sc_srv_voice_verify_proc(SC_MSG_TAG_ST *pstMsg, SC_SRV_CB *pstSCB, SC_SCB_TAG_ST *pstSubServ)
{
    return DOS_SUCC;
}

/**
 * ��������ҵ��״̬������
 *
 * @param SC_MSG_HEAD_ST *pstMsg ��ǰ�¼�
 * @param SC_SRV_CB *pstSCB      ��ǰҵ����ƿ�
 * @param SC_SRV_INFO_ST *pstSubServ ��������ҵ����ƿ�
 *
 * return �ɹ�����DOS_SUCC�����򷵻�DOS_FALSE
 */
U32 sc_srv_access_code_proc(SC_MSG_TAG_ST *pstMsg, SC_SRV_CB *pstSCB, SC_SCB_TAG_ST *pstSubServ)
{
    return DOS_SUCC;
}

/**
 * ��������ҵ��״̬������
 *
 * @param SC_MSG_HEAD_ST *pstMsg ��ǰ�¼�
 * @param SC_SRV_CB *pstSCB      ��ǰҵ����ƿ�
 * @param SC_SRV_INFO_ST *pstSubServ ��������ҵ����ƿ�
 *
 * return �ɹ�����DOS_SUCC�����򷵻�DOS_FALSE
 */
U32 sc_srv_hold_proc(SC_MSG_TAG_ST *pstMsg, SC_SRV_CB *pstSCB, SC_SCB_TAG_ST *pstSubServ)
{
    return DOS_SUCC;
}

/**
 * ��������ҵ��״̬������
 *
 * @param SC_MSG_HEAD_ST *pstMsg ��ǰ�¼�
 * @param SC_SRV_CB *pstSCB      ��ǰҵ����ƿ�
 * @param SC_SRV_INFO_ST *pstSubServ ��������ҵ����ƿ�
 *
 * return �ɹ�����DOS_SUCC�����򷵻�DOS_FALSE
 */
U32 sc_srv_transfer_proc(SC_MSG_TAG_ST *pstMsg, SC_SRV_CB *pstSCB, SC_SCB_TAG_ST *pstSubServ)
{
    return DOS_SUCC;
}

/**
 * ��������ҵ��״̬������
 *
 * @param SC_MSG_HEAD_ST *pstMsg ��ǰ�¼�
 * @param SC_SRV_CB *pstSCB      ��ǰҵ����ƿ�
 * @param SC_SRV_INFO_ST *pstSubServ ��������ҵ����ƿ�
 *
 * return �ɹ�����DOS_SUCC�����򷵻�DOS_FALSE
 */
U32 sc_srv_incoming_queue_proc(SC_MSG_TAG_ST *pstMsg, SC_SRV_CB *pstSCB, SC_SCB_TAG_ST *pstSubServ)
{
    return DOS_SUCC;
}


/**
 * ��������ҵ��״̬������
 *
 * @param SC_MSG_HEAD_ST *pstMsg ��ǰ�¼�
 * @param SC_SRV_CB *pstSCB      ��ǰҵ����ƿ�
 * @param SC_SRV_INFO_ST *pstSubServ ��������ҵ����ƿ�
 *
 * return �ɹ�����DOS_SUCC�����򷵻�DOS_FALSE
 */
U32 sc_srv_interception_proc(SC_MSG_TAG_ST *pstMsg, SC_SRV_CB *pstSCB, SC_SCB_TAG_ST *pstSubServ)
{
    return DOS_SUCC;
}

/**
 * ��������ҵ��״̬������
 *
 * @param SC_MSG_HEAD_ST *pstMsg ��ǰ�¼�
 * @param SC_SRV_CB *pstSCB      ��ǰҵ����ƿ�
 * @param SC_SRV_INFO_ST *pstSubServ ��������ҵ����ƿ�
 *
 * return �ɹ�����DOS_SUCC�����򷵻�DOS_FALSE
 */
U32 sc_srv_whisoered_proc(SC_MSG_TAG_ST *pstMsg, SC_SRV_CB *pstSCB, SC_SCB_TAG_ST *pstSubServ)
{
    return DOS_SUCC;
}

/**
 * ��������ҵ��״̬������
 *
 * @param SC_MSG_HEAD_ST *pstMsg ��ǰ�¼�
 * @param SC_SRV_CB *pstSCB      ��ǰҵ����ƿ�
 * @param SC_SRV_INFO_ST *pstSubServ ��������ҵ����ƿ�
 *
 * return �ɹ�����DOS_SUCC�����򷵻�DOS_FALSE
 */
U32 sc_srv_mark_custom_proc(SC_MSG_TAG_ST *pstMsg, SC_SRV_CB *pstSCB, SC_SCB_TAG_ST *pstSubServ)
{
    return DOS_SUCC;
}

/**
 * ����ҵ���Ӳ㷢�������¼��������¼���ָʾ��ҵ������ִ�в�ͬҵ�����͵�״̬��, ����ж��ҵ����Ҫִ��ҵ��ջ������
 *
 * @param SC_MSG_HEAD_ST *pstMsg ʱ����Ϣͷ
 *
 * @return NULL
 */
VOID sc_evt_process(SC_MSG_TAG_ST *pstMsg)
{
    SC_MSG_EVT_CALL_ST *pstEventCallSetup = NULL;
    SC_SRV_CB          *pstSCB = NULL;
    SC_LEG_CB          *pstLCB = NULL;
    U32                ulSCBNo = U32_BUTT;
    U32                ulRet = DOS_FAIL;
    U32                ulCurrentSrv;

    if (DOS_ADDR_INVALID(pstMsg))
    {
        DOS_ASSERT(0);
        return;
    }

    /* ����Ǻ��룬����Ҫ����ҵ����ƿ� */
    if (SC_EVT_CALL_SETUP == pstMsg->ulMsgType)
    {
        if (pstMsg->ulSCBNo >= SC_SCB_SIZE)
        {
            sc_log(LOG_LEVEL_DEBUG, "Call setup event. alloc new scb");

            pstEventCallSetup = (SC_MSG_EVT_CALL_ST *)pstMsg;
            pstLCB = sc_lcb_get(pstEventCallSetup->ulLegNo);
            if (DOS_ADDR_INVALID(pstLCB))
            {
                DOS_ASSERT(0);
                return;
            }

            pstSCB = sc_scb_alloc();
            if (DOS_ADDR_INVALID(pstSCB))
            {
                sc_req_hungup_with_sound(U32_BUTT, pstEventCallSetup->ulLegNo, CC_ERR_SC_SYSTEM_BUSY);
                return;
            }

            pstSCB->stCall.stSCBTag.bValid = DOS_TRUE;
            pstSCB->stCall.stSCBTag.usStatus = SC_CALL_PORC;
            pstSCB->stCall.ulCallingLegNo = pstEventCallSetup->ulLegNo;
            pstSCB->pstServiceList[pstSCB->ulCurrentSrv] = &pstSCB->stCall.stSCBTag;
            pstLCB->ulSCBNo = pstSCB->ulSCBNo;
        }
        else
        {
            sc_log(LOG_LEVEL_DEBUG, "Call setup event with scb %u", pstMsg->ulSCBNo);

            pstSCB = sc_scb_get(pstMsg->ulSCBNo);
            if (DOS_ADDR_INVALID(pstSCB))
            {
                sc_log(LOG_LEVEL_ERROR, "Get SCB fail. %u", pstMsg->ulSCBNo);
                return;
            }
        }
    }
    else
    {
        sc_log(LOG_LEVEL_DEBUG, "Event with scb %u", pstMsg->ulMsgType, pstMsg->ulSCBNo);

        pstSCB = sc_scb_get(pstMsg->ulSCBNo);
        if (DOS_ADDR_INVALID(pstSCB))
        {
            sc_log(LOG_LEVEL_ERROR, "Get SCB fail. %u", pstMsg->ulSCBNo);
            return;
        }
    }

    ulSCBNo = pstMsg->ulSCBNo;

    sc_trace_scb(pstSCB, "Processing sc event. type: %u. scb: %u, errno: %u"
                    , pstMsg->ulMsgType, pstMsg->ulSCBNo, pstMsg->usInterErr);

    /* ʹ��ѭ������ҵ��ջ */
    while (1)
    {
        /* ���ҵ��ջ��ֻ��һ��ҵ�������ҵ�񲻺Ϸ�����Ҫ�ͷ� */
        ulCurrentSrv = pstSCB->ulCurrentSrv;
        if (0 == pstSCB->ulCurrentSrv && !pstSCB->pstServiceList[ulCurrentSrv]->bValid)
        {
            sc_scb_free(pstSCB);
            break;
        }

        ulRet = DOS_FAIL;

        switch (pstSCB->pstServiceList[ulCurrentSrv]->usSrvType)
        {
            case SC_SRV_CALL:
                ulRet = sc_srv_call_proc(pstMsg, pstSCB, pstSCB->pstServiceList[ulCurrentSrv]);
                break;

            case SC_SRV_PREVIEW_CALL:
                ulRet = sc_srv_call_proc(pstMsg, pstSCB, pstSCB->pstServiceList[ulCurrentSrv]);
                break;

            case SC_SRV_AUTO_CALL:
                ulRet = sc_srv_call_proc(pstMsg, pstSCB, pstSCB->pstServiceList[ulCurrentSrv]);
                break;

            case SC_SRV_VOICE_VERIFY:
                ulRet = sc_srv_call_proc(pstMsg, pstSCB, pstSCB->pstServiceList[ulCurrentSrv]);
                break;

            case SC_SRV_ACCESS_CODE:
                ulRet = sc_srv_call_proc(pstMsg, pstSCB, pstSCB->pstServiceList[ulCurrentSrv]);
                break;

            case SC_SRV_HOLD:
                ulRet = sc_srv_call_proc(pstMsg, pstSCB, pstSCB->pstServiceList[ulCurrentSrv]);
                break;

            case SC_SRV_TRANSFER:
                ulRet = sc_srv_call_proc(pstMsg, pstSCB, pstSCB->pstServiceList[ulCurrentSrv]);
                break;

            case SC_SRV_INCOMING_QUEUE:
                ulRet = sc_srv_call_proc(pstMsg, pstSCB, pstSCB->pstServiceList[ulCurrentSrv]);
                break;

            case SC_SRV_INTERCEPTION:
                ulRet = sc_srv_call_proc(pstMsg, pstSCB, pstSCB->pstServiceList[ulCurrentSrv]);
                break;

            case SC_SRV_WHISPER:
                ulRet = sc_srv_call_proc(pstMsg, pstSCB, pstSCB->pstServiceList[ulCurrentSrv]);
                break;

            case SC_SRV_MARK_CUSTOM:
                ulRet = sc_srv_call_proc(pstMsg, pstSCB, pstSCB->pstServiceList[ulCurrentSrv]);
                break;
            default:
                sc_trace_scb(pstSCB, "Invalid service type : %u", pstSCB->pstServiceList[ulCurrentSrv]->usSrvType);
                break;
        }

        /* ������ش�����Ҫ�ڷ���֮ǰ��SCB�ͷŵ� */
        if (ulRet != DOS_SUCC)
        {
            sc_log(LOG_LEVEL_ERROR, "Process event fail.");
            break;
        }

        if (!SC_SCB_IS_VALID(pstSCB))
        {
            break;
        }


        /* �������ҵ����ջ���������� */
        if (pstSCB->ulCurrentSrv > ulCurrentSrv)
        {
            continue;
        }

        /* �����ǰҵ�񼴽��˳�������Ҫ��ջ */
        if (pstSCB->pstServiceList[ulCurrentSrv]->bWaitingExit)
        {
            pstSCB->pstServiceList[ulCurrentSrv]->bWaitingExit = DOS_FALSE;

            if (pstSCB->ulCurrentSrv > 0)
            {
                pstSCB->ulCurrentSrv--;
            }
            else
            {
                break;
            }
        }
        else
        {
            break;
        }
    }

    sc_trace_scb(pstSCB, "Processed sc event. type: %u.", pstMsg->ulMsgType);
}

/**
 * ά��ҵ���Ӳ㷢�������¼�����Ϣ����
 *
 * @return VOID
 */
VOID *sc_evt_process_runtime(VOID *ptr)
{
    struct timespec     stTimeout;
    DLL_NODE_S    *pstDLLNode = NULL;

    while (1)
    {
        pthread_mutex_lock(&g_mutexEventQueue);
        stTimeout.tv_sec = time(0) + 1;
        stTimeout.tv_nsec = 0;
        pthread_cond_timedwait(&g_condEventQueue, &g_mutexEventQueue, &stTimeout);
        pthread_mutex_unlock(&g_mutexEventQueue);

        while(1)
        {
            if (DLL_Count(&g_stEventQueue) == 0)
            {
                break;
            }

            pthread_mutex_lock(&g_mutexEventQueue);
            pstDLLNode = dll_fetch(&g_stEventQueue);
            pthread_mutex_unlock(&g_mutexEventQueue);

            if (DOS_ADDR_INVALID(pstDLLNode))
            {
                break;
            }

            if (DOS_ADDR_INVALID(pstDLLNode->pHandle))
            {
                DOS_ASSERT(0);

                DLL_Init_Node(pstDLLNode);
                dos_dmem_free(pstDLLNode);
                pstDLLNode = NULL;

                continue;
            }

            sc_evt_process((SC_MSG_TAG_ST *)pstDLLNode->pHandle);

            dos_dmem_free(pstDLLNode->pHandle);
            pstDLLNode->pHandle = NULL;
            DLL_Init_Node(pstDLLNode);
            dos_dmem_free(pstDLLNode);
            pstDLLNode = NULL;
        }
    }

}

/**
 * ҵ����Ʋ��ʼ������
 *
 * @return �ɹ�����DOS_SUCC�����򷵻�DOS_FAIL
 */
U32 sc_event_init()
{
    U32 ulIndex;

    DLL_Init(&g_stEventQueue);

    g_pstSCBList = (SC_SRV_CB *)dos_dmem_alloc(sizeof(SC_SRV_CB) * SC_SCB_SIZE);
    if (DOS_ADDR_INVALID(g_pstSCBList))
    {
        DOS_ASSERT(0);
        return DOS_FALSE;
    }

    for (ulIndex=0; ulIndex<SC_SCB_SIZE; ulIndex++)
    {
        g_pstSCBList[ulIndex].ulSCBNo = ulIndex;
        sc_scb_init(&g_pstSCBList[ulIndex]);
    }

    return DOS_SUCC;
}

/**
 * ҵ����Ʋ���������
 *
 * @return �ɹ�����DOS_SUCC�����򷵻�DOS_FAIL
 */
U32 sc_event_start()
{
    if (pthread_create(&g_pthEventQueue, NULL, sc_evt_process_runtime, NULL) < 0)
    {
        DOS_ASSERT(0);
        return DOS_FAIL;
    }

    return DOS_SUCC;
}

/**
 * ҵ����Ʋ�ֹͣ����
 *
 * @return �ɹ�����DOS_SUCC�����򷵻�DOS_FAIL
 */
U32 sc_event_stop()
{
    return DOS_SUCC;
}


#ifdef __cplusplus
}
#endif /* End of __cplusplus */
