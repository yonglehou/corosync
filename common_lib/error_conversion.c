
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
	case ENOBUFS:
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

const char * cs_strerror(cs_error_t err)
{
	switch (err) {
	case CS_OK:
		return "success";

	case CS_ERR_LIBRARY:
		return "CS_ERR_LIBRARY";

	case CS_ERR_VERSION:
		return "CS_ERR_VERSION";

	case CS_ERR_INIT:
		return "CS_ERR_INIT";

	case CS_ERR_NO_MEMORY:
		return "CS_ERR_NO_MEMORY";

	case CS_ERR_NAME_TOO_LONG :
		return "CS_ERR_NAME_TOO_LONG ";

	case CS_ERR_TIMEOUT:
		return "CS_ERR_TIMEOUT";

	case CS_ERR_TRY_AGAIN:
		return "CS_ERR_TRY_AGAIN";

	case CS_ERR_INVALID_PARAM:
		return "CS_ERR_INVALID_PARAM";

	case CS_ERR_BAD_HANDLE:
		return "CS_ERR_BAD_HANDLE";

	case CS_ERR_BUSY :
		return "CS_ERR_BUSY ";

	case CS_ERR_ACCESS :
		return "CS_ERR_ACCESS ";

	case CS_ERR_NOT_EXIST :
		return "CS_ERR_NOT_EXIST ";

	case CS_ERR_EXIST :
		return "CS_ERR_EXIST ";

	case CS_ERR_NO_SPACE :
		return "CS_ERR_NO_SPACE ";

	case CS_ERR_INTERRUPT :
		return "CS_ERR_INTERRUPT ";

	case CS_ERR_NAME_NOT_FOUND :
		return "CS_ERR_NAME_NOT_FOUND ";

	case CS_ERR_NO_RESOURCES :
		return "CS_ERR_NO_RESOURCES ";

	case CS_ERR_NOT_SUPPORTED :
		return "CS_ERR_NOT_SUPPORTED ";

	case CS_ERR_BAD_OPERATION :
		return "CS_ERR_BAD_OPERATION ";

	case CS_ERR_FAILED_OPERATION :
		return "CS_ERR_FAILED_OPERATION ";

	case CS_ERR_MESSAGE_ERROR :
		return "CS_ERR_MESSAGE_ERROR ";

	case CS_ERR_QUEUE_FULL :
		return "CS_ERR_QUEUE_FULL ";

	case CS_ERR_QUEUE_NOT_AVAILABLE :
		return "CS_ERR_QUEUE_NOT_AVAILABLE ";

	case CS_ERR_BAD_FLAGS :
		return "CS_ERR_BAD_FLAGS ";

	case CS_ERR_TOO_BIG :
		return "CS_ERR_TOO_BIG ";

	case CS_ERR_NO_SECTIONS :
		return "CS_ERR_NO_SECTIONS ";

	case CS_ERR_CONTEXT_NOT_FOUND :
		return "CS_ERR_CONTEXT_NOT_FOUND ";

	case CS_ERR_TOO_MANY_GROUPS :
		return "CS_ERR_TOO_MANY_GROUPS ";

	case CS_ERR_SECURITY :
		return "CS_ERR_SECURITY ";

	default:
		return "unknown error";
	}
}
