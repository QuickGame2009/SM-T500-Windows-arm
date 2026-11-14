#pragma once
#include <ntddk.h>

/* Configuración de arranque de los Bosch BMI270 / BMI260 (8192 bytes cada una). */
#define BMI2_CONFIG_SIZE 8192
extern const UCHAR g_Bmi270Config[BMI2_CONFIG_SIZE];
extern const UCHAR g_Bmi260Config[BMI2_CONFIG_SIZE];
