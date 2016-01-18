/**
 * @file : sc_su.h
 *
 *            (C) Copyright 2014, DIPCC . Co., Ltd.
 *                    ALL RIGHTS RESERVED
 *
 * ����������ݽṹ
 *
 *
 * @date: 2016��1��13��
 * @arthur: Larry
 */

#ifndef __SC_SU_H__
#define __SC_SU_H__

#ifdef __cplusplus
extern "C" {
#endif /* End of __cplusplus */


U32 sc_esl_event_create(esl_event_t *pstEvent);
U32 sc_esl_event_answer(esl_event_t *pstEvent, SC_LEG_CB *pstLegCB);
U32 sc_esl_event_hangup(esl_event_t *pstEvent, SC_LEG_CB *pstLegCB);
U32 sc_esl_event_progress(esl_event_t *pstEvent, SC_LEG_CB *pstLegCB);
U32 sc_esl_event_park(esl_event_t *pstEvent, SC_LEG_CB *pstLegCB);
U32 sc_esl_event_hold(esl_event_t *pstEvent, SC_LEG_CB *pstLegCB);
U32 sc_esl_event_unhold(esl_event_t *pstEvent, SC_LEG_CB *pstLegCB);
U32 sc_esl_event_dtmf(esl_event_t *pstEvent, SC_LEG_CB *pstLegCB);
U32 sc_esl_event_record_start(esl_event_t *pstEvent, SC_LEG_CB *pstLegCB);
U32 sc_esl_event_record_stop(esl_event_t *pstEvent, SC_LEG_CB *pstLegCB);
U32 sc_esl_event_playback_start(esl_event_t *pstEvent, SC_LEG_CB *pstLegCB);
U32 sc_esl_event_playback_stop(esl_event_t *pstEvent, SC_LEG_CB *pstLegCB);

U32 sc_esl_event_background_job(esl_event_t *pstEvent);
U32 sc_esl_event_shutdown(esl_event_t *pstEvent);


U32 sc_esl_execute(const S8 *pszApp, const S8 *pszArg, const S8 *pszUUID);
U32 sc_esl_execute_cmd(const S8 *pszCmd, S8 *pszUUID, U32 ulLenght);


U32 sc_bgjob_hash_find(S8 *pszUUID);
U32 sc_bgjob_hash_delete(S8 *pszUUID);

#ifdef __cplusplus
}
#endif /* End of __cplusplus */


#endif
