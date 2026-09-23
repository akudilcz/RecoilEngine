#ifndef SYNC_UPDATED_PATHS_SYSTEM_UTILS_H_
#define SYNC_UPDATED_PATHS_SYSTEM_UTILS_H_

#include <utility>

#include "Sim/Path/QTPFS/Registry.h"

namespace QTPFS {

	inline void FinishPathSearch
        ( PathManager* pm
		, PathSearch* search
        )
	{
        auto completePath = [pm](QTPFS::entity pathEntity, IPath* path){

            // Transfer search update path, to the simulation-visible path. A swap (rather than a
            // move) is used so the now-stale live-path buffers land back in the SearchModeIPath
            // slot instead of being left empty; PrepareSearchForFinalize's copy into that slot on
            // the path's next search can then reuse that capacity instead of reallocating.
            std::swap(*path, static_cast<IPath&>(registry.get<SearchModeIPath>(pathEntity)));

            // inform the movement system that the path has been changed.
            if (registry.all_of<PathUpdatedCounterIncrease>(pathEntity)) {
                path->SetNumPathUpdates(path->GetNumPathUpdates() + 1);
                path->SetNextPointIndex(0);
                registry.remove<PathUpdatedCounterIncrease>(pathEntity);
            }
            registry.remove<PathIsTemp>(pathEntity);
            registry.remove<PathIsDirty>(pathEntity);
            registry.remove<PathSearchRef>(pathEntity);

            // If the node data wasn't recorded, then the path isn't shareable.
            if (!path->IsBoundingBoxOverriden() || path->GetNodeList().size() == 0) {
                pm->RemovePathFromShared(pathEntity);
                pm->RemovePathFromPartialShared(pathEntity);
            }
        };

		QTPFS::entity pathEntity = (QTPFS::entity)search->GetID();
		if (registry.valid(pathEntity)) {
			// Only owned paths should be actioned in this function.
			IPath* path = registry.try_get<IPath>(pathEntity);
			if (path != nullptr) {
				if (search->PathWasFound()) {
					completePath(pathEntity, path);
					// LOG("%s: %x - path found", __func__, entt::to_integral(pathEntity));
				} else {
					if (search->rawPathCheck) {
						registry.remove<PathSearchRef>(pathEntity);
						registry.remove<PathIsDirty>(pathEntity);

						// adding a new search doesn't break this loop because new paths do not
						// have the tag ProcessPath and so don't impact this group view.
						pm->RequeueSearch(path, false, true, search->tryPathRepair);
						// LOG("%s: %x - raw path check failed", __func__, entt::to_integral(pathEntity));
					} else if (search->pathRequestWaiting) {
						// nothing to do - it will be rerun next frame
						// LOG("%s: %x - waiting for partial root path", __func__, entt::to_integral(pathEntity));
						// continue;
						registry.remove<PathSearchRef>(pathEntity);
						pm->RequeueSearch(path, false, search->allowPartialSearch, false);
					} else if (search->rejectPartialSearch) {
						registry.remove<PathSearchRef>(pathEntity);
						pm->RequeueSearch(path, false, false, false);
					}
					else {
						// LOG("%s: %x - search failed", __func__, entt::to_integral(pathEntity));
						// Don't invalid the path, now, give the unit the chance to escape from
						// being stuck inside something.
						path->SetBoundingBox();
						completePath(pathEntity, path);
					}
				}
			}
		}
	}

}

#endif

