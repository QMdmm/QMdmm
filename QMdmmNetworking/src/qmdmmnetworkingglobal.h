// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef QMDMMNETWORKINGGLOBAL_H
#define QMDMMNETWORKINGGLOBAL_H

#include <QMdmmCoreGlobal>

QMDMM_EXPORT_NAME(QMdmmNetworkingGlobal)

#ifndef DOXYGEN
// QMDMMNETWORKING_EXPORT comes from a generated header rather than from this one; see the
// export header generation in this module's CMakeLists.txt. Doxygen must not read it, for the
// same reason as in QMdmmCore: the macro would reach the published headers expanded. The
// DOXYGEN branch below keeps it an empty macro on the published side.
#include <QMdmmNetworking/qmdmmnetworking_export.h>
#ifdef QMDMM_NEED_EXPORT_PRIVATE
#define QMDMMNETWORKING_PRIVATE_EXPORT QMDMMNETWORKING_EXPORT
#else
#define QMDMMNETWORKING_PRIVATE_EXPORT
#endif
#else
#define QMDMMNETWORKING_EXPORT
#define QMDMMNETWORKING_PRIVATE_EXPORT
#endif

namespace QMdmmNetworking {
#ifndef DOXYGEN
namespace p {
}
namespace v0 {
}
inline namespace v1 {
}
#endif
} // namespace QMdmmNetworking

#endif // QMDMMNETWORKINGGLOBAL_H
