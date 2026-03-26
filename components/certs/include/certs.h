#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// These PEM blobs are intentionally empty in source control.
// Provision them from a local build overlay or another out-of-repo source.
extern const char aws_root_ca_pem[];
extern const char aws_device_cert_pem[];
extern const char aws_device_private_key_pem[];

bool certs_are_provisioned(void);

#ifdef __cplusplus
}
#endif
