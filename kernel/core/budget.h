/* budget.h - die Budgets des Kernels, eine Wahrheit für Kernel, tools/budget.py
 * und docs/00-vision.md. Ändern nur zusammen mit dem Dokument. */
#ifndef RC_BUDGET_H
#define RC_BUDGET_H

#define RC_BUDGET_IMAGE_LIMIT_KIB   1024   /* harte Grenze: Kernel-Image */
#define RC_BUDGET_IMAGE_TARGET_KIB   256   /* Ziel: Kernel-Image */
#define RC_BUDGET_CODE_TARGET_KIB    120   /* Code und Konstanten im RAM */
#define RC_BUDGET_DATA_TARGET_KIB     64   /* Daten, 48 KiB + 16 KiB für eine CPU */
#define RC_BUDGET_TABLES_TARGET_KIB   48   /* eigene Seitentabellen: Direktabbildung, Kernel, MMIO */
#define RC_BUDGET_LINES_TARGET     10000   /* Codebudget: Zeilen C und Assembler */

#endif
