#pragma once

#include "General.h"
#include "Board.h"

/**************************************************************************************************/
/*                                                                                                */
/*                                       CONSTANTS & MACROS                                       */
/*                                                                                                */
/**************************************************************************************************/

/**************************************************************************************************/
/*                                                                                                */
/*                                          TYPE DEFINITIONS                                      */
/*                                                                                                */
/**************************************************************************************************/

typedef struct /* 16 bytes */
{
    SQUARE tdir;
    UINT   tbit;
    UINT   tbit_;
    UINT   mbit;
    UINT   mbit_;
    INT    dm; // Mobility change for rook/bishop
    UINT   unused1;
    UINT   unused2;
} RBDATA;

typedef struct /* 32 bytes */
{
    SQUARE tdir0;
    UINT   tbit0;
    UINT   tbit0_;
    SQUARE tdir1;
    UINT   tbit1;
    UINT   tbit1_;
    SQUARE tdir2;
    UINT   tbit2;
    UINT   tbit2_;
    UINT   mbit;
    UINT   mbit_;
    UINT   padding[5];
} QDATA;

typedef struct
{
    RBDATA RBUpdData_[18], RBUpdData[18];
    QDATA  QUpdData_[18], QUpdData[18];
} PERFORMMOVE_COMMON;
