#ifndef MUTGOS_DBINTERFACE_UPDATE_MANAGER_H
#define MUTGOS_DBINTERFACE_UPDATE_MANAGER_H

#include <map>
#include <list>

#include <boost/thread/mutex.hpp>
#include <boost/thread/thread.hpp>
#include <boost/lockfree/queue.hpp>
#include <boost/atomic/atomic.hpp>

#include "dbtypes/dbtype_Id.h"
#include "dbtypes/dbtype_Entity.h"
#include "dbtypes/dbtype_DatabaseEntityChangeListener.h"
#include "dbinterface_EntityRef.h"
#include "dbtypes/dbtype_EntityField.h"

namespace mutgos
{
namespace dbinterface
{
    /**
     * Basically, this listens to all Entity updates and deletes, and commits
     * them to the database.  In the case of deletes, it will also delete
     * anything contained by the Entity.  The program that issues the delete
     * is responsible for moving anything out that shouldn't be deleted.
     *
     * Pending operations are queued for periodic processing on a thread.
     * In the future this could allow for some sort of transaction-based updates
     * to preserve integrity.
     */
    class UpdateManager : public dbtype::DatabaseEntityChangeListener
    {
    public:
        /**
         * Creates the singleton if it doesn't already exist.
         * @return The singleton instance.
         */
        static UpdateManager *make_singleton(void);

        /**
         * Will NOT create singleton if it doesn't already exist.
         * @return The singleton instance, or null if not created.
         */
        static UpdateManager *instance()
          { return singleton_ptr; }

        /**
         * Destroys the singleton instance if it exists, calling shutdown()
         * as needed.
         */
        static void destroy_singleton(void);

        /**
         * Used by Boost threads to start our threaded code.
         */
        void operator()();

        /**
         * Initializes the singleton instance; called once as MUTGOS is coming
         * up.
         */
        void startup(void);

        /**
         * Shuts down the singleton instance; called when MUTGOS is coming down.
         */
        void shutdown(void);

        /**
         * Called when the provided entity has changed in some way.
         * Each attribute changed on an entity will cause this to be called,
         * however several changes may be in a single call.
         * @param entity[in] The entity that has changed.
         * @param fields_changed[in] The fields that have changed.
         * @param flags_changed[in] Detailed information on what flags have
         * changed.
         * @param ids_changed[in] Detailed information about changes concerning
         * fields of type ID (or lists of IDs).
         */
        virtual void entity_changed(
            dbtype::Entity *entity,
            const dbtype::Entity::EntityFieldSet &fields_changed,
            const dbtype::Entity::FlagsRemovedAdded &flags_changed,
            const dbtype::Entity::ChangedIdFieldsMap &ids_changed);

        /**
         * Adds the given Entity IDs to the list of pending database deletes.
         * The Entities must already be marked as deleted and have all
         * pre-processing done (which at this point is to mark all Entities
         * in the set as deleted and to include all Entities contained by each
         * Entity in this set as well).
         * At a later point in time, each Entity will automatically have
         * references removed and will be deleted from the database.
         * @param entities[in] The IDs of the Entitys to delete.
         */
        void entities_deleted(const dbtype::Entity::IdSet &entities);

        /**
         * Used to determine if an Entity is in the process of being deleted.
         * If Entity is being deleted, do not try and bring it back into
         * the database cache, or deletion will fail!  Other database areas
         * can use this method before trying to retrieve an Entity to make sure
         * it's 'safe'.
         * @param entity_id[in] The ID of the Entity to check.
         * @return True if Entity is in the process of being deleted.
         */
        bool is_entity_delete_pending(const dbtype::Id &entity_id);

        /**
         * Adds the given site ID to the list of pending database delete
         * commits.  The site must already have been marked as delete pending.
         * At a later point in time, the site will be automatically deleted from
         * the database.
         * @param site_id[in] The site to delete.
         */
        void site_deleted(const dbtype::Id::SiteIdType site_id);

        /**
         * Main loop of UpdateManager thread.
         */
        void thread_main(void);

    private:

        typedef std::list<dbtype::Id> IdList;

        /**
         * Container class to hold all the updates pending for a given
         * Entity.
         */
        class EntityUpdate
        {
        public:
            /**
             * Container constructor.
             * @param entity The Entity the update is for.
             */
            EntityUpdate(dbtype::Entity *entity);

            /**
             * Takes the provided changes and merges them into what's
             * already in this instance.
             * @param fields[in] The fields that have changed.
             * @param flags[in] Detailed information on what flags have changed.
             * @param ids[in] Detailed information about changes
             * concerning fields of type ID (or lists of IDs).
             */
            void merge_update(
                const dbtype::Entity::EntityFieldSet &fields,
                const dbtype::Entity::FlagsRemovedAdded &flags,
                const dbtype::Entity::ChangedIdFieldsMap &ids);

            const dbtype::Id entity_id; ///< ID of the Entity being updated
            dbtype::Entity::EntityFieldSet fields_changed; ///< Changed fields
            dbtype::Entity::FlagsRemovedAdded flags_changed; ///< Changed flags
            dbtype::Entity::ChangedIdFieldsMap ids_changed; ///< ID Sets changed
        };

        typedef std::map<dbtype::Id, EntityUpdate *> PendingUpdatesMap;

        /**
         * Given the IDs added and removed on a given Entity, update the
         * Entities it is referencing with a back-reference.
         * The lock mutex is assumed to be UNLOCKED.
         * @param id[in] The Entity whose fields are being updated.
         * @param changed_fields[in] The fields which have IDs being added
         * and/or removed.
         */
        void process_id_references(
            const dbtype::Id &id,
            const dbtype::Entity::ChangedIdFieldsMap changed_fields);

        /**
         * Removes all references to the provided Entity.
         * @param entity[in] The Entity to remove all references to.
         */
        void remove_all_references(EntityRef &entity);

        /**
         * Given a valid target and source, remove the target from the source's
         * field.
         * @param target[in] The target to be removed from the source's field.
         * @param source[in,out] The source to remove the target from.
         * @param field[in] The field to remove the target from.
         */
        void remove_reference_from_source(
            const dbtype::Id &target,
            EntityRef &source,
            const dbtype::EntityField field);

        /**
         * Given a source Entity who has a field that references a target
         * entity, remove the back-reference from the target.
         * @param source[in] The source Entity (making the reference).
         * @param target[in] The target Entity (has a back-reference).
         * @param field[in] The field which has the reference.
         */
        void remove_reference(
            const dbtype::Id &source,
            const dbtype::Id &target,
            const dbtype::EntityField field);

        /**
         * Singleton constructor.
         */
        UpdateManager(void);

        /**
         * Singleton destructor.
         */
        virtual ~UpdateManager();

        static UpdateManager *singleton_ptr; ///< Singleton pointer.

        boost::mutex mutex; ///< Enforces single access at a time.
        boost::thread *thread_ptr; ///< Non-null when thread is running.
        PendingUpdatesMap pending_updates; ///< Updates to be committed
        dbtype::Entity::IdSet pending_deletes; ///< Deletes to be committed
        dbtype::Id::SiteIdVector pending_site_deletes; ///< Pending site deletes
        boost::atomic<bool> shutdown_thread_flag; ///< True if thread should shutdown
    };

}
}

#endif //MUTGOS_DBINTERFACE_UPDATE_MANAGER_H
