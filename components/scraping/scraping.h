#pragma once
#include <stddef.h>

/*
 * Виконує HTTP GET на url із заголовком "Authorization: Bearer <token>" та
 * "Accept: application/json". Тіло відповіді копіюється в outBuf (не більше
 * outBufSize-1 байт + завершальний '\0') - буфер виділяє й передає викликач,
 * функція гарантовано не вийде за його межі.
 *
 * Повертає HTTP-статус-код (200, 401, 404, ...), або -1 при помилці
 * транспортного рівня (немає з'єднання, тайм-аут, TLS-помилка тощо).
 */
int api_fetch_bearer_auth(const char *url, const char *token, char *outBuf, size_t outBufSize);
