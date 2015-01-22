/*
 *            (C) Copyright 2014, ����Ѷͨ . Co., Ltd.
 *                    ALL RIGHTS RESERVED
 *
 *  cfg_mod_ptc.h
 *
 *  Created on: 2014-11-17
 *      Author: Larry
 *        Todo: ���ü�ؽ�������ص�ģ��Ŀ���
 *     History:
 */

#ifndef __CFG_MOD_PTC_H__
#define __CFG_MOD_PTC_H__

/* �쳣����ģ�� */
#define INCLUDE_EXCEPTION_CATCH           1

#define INCLUDE_DEBUG_CLI_SERVER          0
#define INCLUDE_DEBUG_CLI                 (!INCLUDE_DEBUG_CLI_SERVER)

/* ��־ģ�鿪�� */
#define INCLUDE_SYSLOG_ENABLE             1
#if (INCLUDE_SYSLOG_ENABLE)
#define INCLUDE_SYSLOG_FILE               0
#define INCLUDE_SYSLOG_DB                 1
#define INCLUDE_SYSLOG_CLI                INCLUDE_DEBUG_CLI
#define INCLUDE_SYSLOG_CONSOLE            1
#else
#define INCLUDE_SYSLOG_FILE               0
#define INCLUDE_SYSLOG_DB                 0
#define INCLUDE_SYSLOG_CLI                0
#define INCLUDE_SYSLOG_CONSOLE            0
#endif


/* ����ģ�鿪�� */
#define INCLUDE_BH_ENABLE                 1
#if (INCLUDE_BH_ENABLE)
#define INCLUDE_BH_SERVER                 1
#define INCLUDE_BH_CLIENT                 (!INCLUDE_BH_SERVER)
#else
#define INCLUDE_BH_SERVER                 0
#define INCLUDE_BH_CLIENT                 0
#endif

/* �Ƿ����dos�ڴ����ģ�� */
#define INCLUDE_MEMORY_MNGT               1

/* xml�����ļ�ģ�� */
#define INCLUDE_XML_CONFIG                1

/* ��ʱ��ģ�� */
#define INCLUDE_SERVICE_TIMER             1

/* PTCģ��*/
#define INCLUDE_PTC                       1


#endif /* END __CFG_MOD_PTC_H__ */
