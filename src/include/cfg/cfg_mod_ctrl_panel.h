/*
 *            (C) Copyright 2014, 天天讯通 . Co., Ltd.
 *                    ALL RIGHTS RESERVED
 *
 *  cfg_mod_ctrl_panel.h
 *
 *  Created on: 2014-11-17
 *      Author: Larry
 *        Todo: 配置控制平台进程中各个模块的开关
 *     History:
 */

#ifndef __CFG_MOD_CTRL_PANEL_H__
#define __CFG_MOD_CTRL_PANEL_H__

/* 异常处理模块 */
#define INCLUDE_EXCEPTION_CATCH           1

#define INCLUDE_DEBUG_CLI_SERVER          1
#define INCLUDE_DEBUG_CLI                 (!INCLUDE_DEBUG_CLI_SERVER)

/* 日志模块开关 */
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


/* 心跳模块开关 */
#define INCLUDE_BH_ENABLE                 1
#if (INCLUDE_BH_ENABLE)
#define INCLUDE_BH_SERVER                 0
#define INCLUDE_BH_CLIENT                 (!INCLUDE_BH_SERVER)
#else
#define INCLUDE_BH_SERVER                 0
#define INCLUDE_BH_CLIENT                 0
#endif

/* 是否包含dos内存管理模块 */
#define INCLUDE_MEMORY_MNGT               1

/* xml配置文件模块 */
#define INCLUDE_XML_CONFIG                1

/* 定时器模块 */
#define INCLUDE_SERVICE_TIMER             1


#endif /* END __CFG_MOD_MONITER_H__ */

