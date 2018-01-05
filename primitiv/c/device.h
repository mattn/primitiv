#ifndef PRIMITIV_C_DEVICE_H_
#define PRIMITIV_C_DEVICE_H_

#include <primitiv/c/define.h>

/**
 * Opaque type of Device.
 */
typedef struct primitiv_Device primitiv_Device;

/**
 * Retrieves the current default device.
 * @param device Pointer to receive the current default device.
 * @return Status code.
 * @remarks The pointer returned by this function is owned by the library, and
 *          should not be passed to `primitiv_Device_delete()`.
 */
PRIMITIV_C_API PRIMITIV_C_STATUS primitiv_Device_get_default(
    primitiv_Device **device);

/**
 * Specifies a new default device.
 * @param device Pointer of the new default device.
 * @return Status code.
 */
PRIMITIV_C_API PRIMITIV_C_STATUS primitiv_Device_set_default(
    primitiv_Device *device);

/**
 * Deletes the Device object.
 * @param device Pointer of a handler.
 * @return Status code.
 */
PRIMITIV_C_API PRIMITIV_C_STATUS primitiv_Device_delete(
    primitiv_Device *device);

/**
 * Prints device description to stderr.
 * @param device Pointer of a handler.
 * @return Status code.
 */
PRIMITIV_C_API PRIMITIV_C_STATUS primitiv_Device_dump_description(
    const primitiv_Device *device);

#endif  // PRIMITIV_C_DEVICE_H_
