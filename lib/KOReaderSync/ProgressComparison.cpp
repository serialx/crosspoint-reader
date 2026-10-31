#include "ProgressComparison.h"

#include "SyncBridge.h"
ProgressComparison compareProgress(const CrossPointPosition& local, float localPercentage,
                                   const CrossPointPosition& remote, float remotePercentage) {
  const auto l = syncPosition(local), r = syncPosition(remote);
  return static_cast<ProgressComparison>(xp_sync_compare(&l, localPercentage, &r, remotePercentage));
}
RemoteRecordChoice selectRemoteRecord(const CrossPointPosition& primary, float primaryPercentage,
                                      const CrossPointPosition& alternate, float alternatePercentage) {
  return compareProgress(primary, primaryPercentage, alternate, alternatePercentage) == ProgressComparison::RemoteAhead
             ? RemoteRecordChoice::Alternate
             : RemoteRecordChoice::Primary;
}
