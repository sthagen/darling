#include "libaks.h"
#include "libaks_smartcard.h"

const CFStringRef kAKSKeyAcl                     = CFSTR("AKSKeyAcl");
const CFStringRef kAKSKeyAclParamRequirePasscode = CFSTR("AKSKeyAclParamRequirePasscode");
const CFStringRef kAKSKeyOpDefaultAcl            = CFSTR("AKSKeyOpDefaultAcl");
const CFStringRef kAKSKeyOpSign                  = CFSTR("AKSKeyOpSign");
const CFStringRef kAKSKeyOpComputeKey            = CFSTR("AKSKeyOpComputeKey");
const CFStringRef kAKSKeyOpAttest                = CFSTR("AKSKeyOpAttest");
const CFStringRef kAKSKeyOpDecrypt               = CFSTR("AKSKeyOpDecrypt");
const CFStringRef kAKSKeyOpEncrypt               = CFSTR("AKSKeyOpEncrypt");
const CFStringRef kAKSKeyOpDelete                = CFSTR("AKSKeyOpDelete");
const CFStringRef kAKSKeyOpECIESTranscode        = CFSTR("AKSKeyOpECIESTranscode");

void aks_smartcard_unregister(int a)
{

}

int aks_smartcard_request_unlock(int a, unsigned char *b, int c, void **d, size_t *e)
{
	return 0;
}

int aks_smartcard_unlock(int a, unsigned char *b, int c, unsigned char *d, int e, void **f, size_t *g)
{
	return 0;
}

int aks_smartcard_register(int a, unsigned char *b, int c, int d, unsigned char *e, int f, void **g, size_t *h)
{
	return 0;
}

int aks_smartcard_get_sc_usk(void *a, int b, const void **c, size_t *d)
{
	return 0;
}

int aks_smartcard_get_ec_pub(void *a, int b, const void **c, size_t *d)
{
	return 0;
}

kern_return_t aks_assert_hold(keybag_handle_t keybagHandle, AKSAssertionType_t lockAssertType, uint64_t timeout)
{
	return KERN_FAILURE;
}

kern_return_t aks_assert_drop(keybag_handle_t keybagHandle, AKSAssertionType_t lockAssertType)
{
	return KERN_FAILURE;
}

kern_return_t aks_create_bag(uint8_t* secret, int secret_size, int bag_type, keybag_handle_t* handle) {
	return KERN_FAILURE;
};

kern_return_t aks_save_bag(keybag_handle_t handle, void** bytes, size_t* size) {
	return KERN_FAILURE;
};

kern_return_t aks_unload_bag(keybag_handle_t handle) {
	return KERN_FAILURE;
};

kern_return_t aks_unlock_bag(keybag_handle_t handle, const void* passcode, int length) {
	return KERN_FAILURE;
};

kern_return_t aks_load_bag(const void* data, int length, keybag_handle_t* handle) {
	return KERN_FAILURE;
};
