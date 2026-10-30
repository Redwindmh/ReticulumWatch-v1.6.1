#pragma once

// ReticulumWatch owns its permanent identity and LXMF history on its dedicated
// FFat partition. microReticulum's internal filesystem persistence and the
// optional Provisioning subsystem remain disabled in M3 so the security
// rebase does not also change persistence/configuration architecture.
//
// This header is force-included for application and dependency translation
// units. Undefining feature flags is deliberate: upstream gates these with
// #ifdef, so defining them to numeric 0 would still enable the code.
#ifdef RNS_USE_FS
#undef RNS_USE_FS
#endif

#ifdef RNS_USE_PROVISIONING
#undef RNS_USE_PROVISIONING
#endif

#ifdef RNS_ENABLE_REMOTE_PROVISIONING
#undef RNS_ENABLE_REMOTE_PROVISIONING
#endif

#ifndef RNS_PERSIST_PATHS
#define RNS_PERSIST_PATHS 0
#endif
#ifndef RNS_PERSIST_KNOWN_DESTINATIONS
#define RNS_PERSIST_KNOWN_DESTINATIONS 0
#endif
#ifndef RNS_PERSIST_HASHLIST
#define RNS_PERSIST_HASHLIST 0
#endif
