
#include <corosync/corotypes.h>

cs_error_t qb_to_cs_error (int result)
{
	int32_t res;
	cs_error_t err = CS_ERR_LIBRARY;

	if (result >= 0) {
		return CS_OK;
	}
	res = -result;

	switch (res) {
	case EBADF:
		err = CS_ERR_BAD_HANDLE;
		break;
	case ENOMEM:
		err = CS_ERR_NO_MEMORY;
		break;
	case ETIMEDOUT:
	case EAGAIN:
		err = CS_ERR_TRY_AGAIN;
		break;
	case EBADE:
		err = CS_ERR_FAILED_OPERATION;
		break;
	case ETIME:
		err = CS_ERR_TIMEOUT;
		break;
	case EINVAL:
		err = CS_ERR_INVALID_PARAM;
		break;
	case EBUSY:
		err = CS_ERR_BUSY;
		break;
	case EACCES:
		err = CS_ERR_ACCESS;
		break;
	case EOVERFLOW:
		err = CS_ERR_NAME_TOO_LONG;
		break;
	case EEXIST:
		err = CS_ERR_EXIST;
		break;
	case ENOBUFS:
		err = CS_ERR_QUEUE_FULL;
		break;
	case ENOSPC:
		err = CS_ERR_NO_SPACE;
		break;
	case EINTR:
		err = CS_ERR_INTERRUPT;
		break;
	case ENOENT:
	case ENODEV:
		err = CS_ERR_NOT_EXIST;
		break;
	case ENOSYS:
	case ENOTSUP:
		err = CS_ERR_NOT_SUPPORTED;
		break;
	case EBADMSG:
		err = CS_ERR_MESSAGE_ERROR;
		break;
	case EMSGSIZE:
	case E2BIG:
		err = CS_ERR_TOO_BIG;
		break;
	case ECONNREFUSED:
	case ENOTCONN:
	default:
		err = CS_ERR_LIBRARY;
		break;
	}

	return err;
}

cs_error_t hdb_error_to_cs (int res)
{
	if (res == 0) {
		return (CS_OK);
	} else {
		if (res == -EBADF) {
			return (CS_ERR_BAD_HANDLE);
		} else if (res == -ENOMEM) {
			return (CS_ERR_NO_MEMORY);
		} else 	if (res == -EMFILE) {
			return (CS_ERR_NO_RESOURCES);
		} else	if (res == -EACCES) {
			return (CS_ERR_ACCESS);
		}
		return (CS_ERR_LIBRARY);
	}
}

