#include "storage/save_session.h"

namespace vse::storage
{

SaveSession::SaveSession()
    : hash_of_(MakeUnique<HashMap<EntityKey, u64, HeapAllocator>>(HeapAllocator::Default())),
      key_of_(MakeUnique<HashMap<ecs::Entity, EntityKey, HeapAllocator>>(HeapAllocator::Default()))
{
}

void SaveSession::Reset()
{
    baseline_ = false;
    world_id_ = 0;
    since_ = 0;
    removal_epoch_ = 0;
    last_removal_seq_ = 0;
    hash_of_->Clear();  // the tables are kept for the next save (container audit Day 4: no re-made maps)
    key_of_->Clear();
}

}  // namespace vse::storage
