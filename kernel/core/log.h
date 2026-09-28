/* log.h - Meldungsbereich der Konsole. */
#ifndef RC_LOG_H
#define RC_LOG_H

#include "kernel.h"

/* Ab jetzt landen klog-Zeilen auch in den Konsolenzeilen [top, bottom). */
void klog_area(int top, int bottom, uint8_t attr);
void klog_line(const char *line);

#endif
