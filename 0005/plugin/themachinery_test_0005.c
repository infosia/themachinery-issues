#include <plugins/gameplay/gameplay.h>

#include <foundation/allocator.h>
#include <foundation/api_registry.h>
#include <foundation/localizer.h>
#include <foundation/the_truth.h>
#include <foundation/log.h>
#include <foundation/task_system.h>
#include <plugins/entity/entity.h>
#include <plugins/entity/scene_tree_component.h>
#include <foundation/string_repository.h>
#include <the_machinery/component_interfaces/editor_ui_interface.h>
#include <foundation/math.inl>

#include "motionclient/motionclient.h"

// short-hand for tm_gameplay_api
static struct tm_gameplay_api *g;
static struct tm_entity_api *tm_entity_api;
static struct tm_the_truth_api *tm_the_truth_api;
static struct tm_logger_api *tm_logger_api;
static struct tm_scene_tree_component_api *tm_scene_tree_component_api;
static struct tm_string_repository_api *tm_string_repository_api;
static struct tm_string_repository_i *tm_string_repository;
static struct tm_task_system_api* tm_task_system_api;

#define PLAYER_NAME_HASH TM_STATIC_HASH("player", 0xafff68de8a0598dfULL)
#define NODE_NOT_FOUND UINT32_MAX

typedef struct tm_gameplay_state_o
{
    tm_scene_tree_component_t *tm_scene_tree_component;
} tm_gameplay_state_o;

static void motionclient_run_task(void* data_, uint64_t task_id)
{
    motionclient_start(tm_string_repository);
}

static void motionclient_run()
{
    if (motionclient_started()) {
        return;
    }

    tm_task_system_api->run_task(motionclient_run_task, NULL, "Start Motion Client");
}

static void start(tm_gameplay_context_t *ctx)
{
    tm_gameplay_state_o *state = ctx->state;
    tm_entity_t player = g->entity->find_entity_with_tag(ctx, PLAYER_NAME_HASH);
    if (player.index > 0) {
        state->tm_scene_tree_component = tm_entity_api->get_component(ctx->entity_ctx, player, tm_entity_api->lookup_component(ctx->entity_ctx, TM_TT_TYPE_HASH__SCENE_TREE_COMPONENT));
    }
}

static void update(tm_gameplay_context_t *ctx)
{
    tm_gameplay_state_o *state = ctx->state;
    tm_scene_tree_component_t* stc = state->tm_scene_tree_component;

    if (stc != NULL) {
        motion_listener_transform_data_t* data = motionclient_poll();
        for (uint32_t i = 0; i < data->count; i++) {
            const uint32_t node_index = tm_scene_tree_component_api->node_index_from_name(stc, data->hashes[i], NODE_NOT_FOUND);
            if (node_index != NODE_NOT_FOUND) {
                tm_transform_t transform = tm_scene_tree_component_api->local_transform(stc, node_index);
                transform.rot = data->rotations[i];

                tm_scene_tree_component_api->set_local_transform(stc, node_index, &transform);
            }
        }
    }
}

// Remainder of file is component set-up.

#define TYPE__THEMACHINERY_TEST_0005_COMPONENT "themachinery_test_0005_component"
#define TYPE_HASH__THEMACHINERY_TEST_0005_COMPONENT TM_STATIC_HASH("themachinery_test_0005_component", 0x4c724c15e4d87326ULL)
#define TEST_0005_SYSTEM_NAME TM_LOCALIZE_LATER("TheMachinery Test 0005")
#define TEST_0005_SYSTEM_NAME_HASH TM_STATIC_HASH("TheMachinery Test 0005", 0x71b6f4fdd37007f2ULL)

typedef struct
{
    tm_entity_context_o *entity_ctx;
    tm_allocator_i allocator;
} gameplay_component_manager_t;

static void system_update(tm_entity_context_o *entity_ctx, tm_gameplay_context_t *ctx)
{
    g->context->update(ctx);

    if (!ctx->initialized)
        return;

    if (!ctx->started) {
        ctx->state = tm_alloc(ctx->allocator, sizeof(*ctx->state));
        *ctx->state = (tm_gameplay_state_o){ NULL };
        start(ctx);
        ctx->started = true;
    }

    update(ctx);
}

static void component_added(gameplay_component_manager_t *manager, tm_entity_t e, tm_gameplay_context_t *ctx)
{
    const bool editor = tm_entity_api->get_blackboard_double(manager->entity_ctx, TM_ENTITY_BB__EDITOR, 0);
    if (editor)
        return;

    tm_allocator_i *a = &manager->allocator;
    g->context->init(ctx, a, manager->entity_ctx);

    const tm_entity_system_i gameplay_system = {
        .name = TEST_0005_SYSTEM_NAME,
        .update = (void (*)(tm_entity_context_o *, tm_entity_system_o *))system_update,
        .inst = (tm_entity_system_o *)ctx
    };

    tm_entity_api->register_system(ctx->entity_ctx, &gameplay_system);

    motionclient_run();
}

static void system_hot_reload(tm_entity_context_o *entity_ctx, tm_entity_system_i *system)
{
    system->update = (void (*)(tm_entity_context_o *, tm_entity_system_o *))system_update;
}

static void component_removed(gameplay_component_manager_t *manager, tm_entity_t e, tm_gameplay_context_t *ctx)
{
    const bool editor = tm_entity_api->get_blackboard_double(manager->entity_ctx, TM_ENTITY_BB__EDITOR, 0);
    if (editor)
        return;

    motionclient_stop(tm_string_repository);

    tm_free(ctx->allocator, ctx->state, sizeof(*ctx->state));
    g->context->shutdown(ctx);
}

static void destroy(gameplay_component_manager_t *manager)
{
    tm_entity_context_o *ctx = manager->entity_ctx;
    tm_entity_api->call_remove_on_all_entities(ctx, tm_entity_api->lookup_component(ctx, TYPE_HASH__THEMACHINERY_TEST_0005_COMPONENT));
    tm_allocator_i a = manager->allocator;
    tm_free(&a, manager, sizeof(*manager));
    tm_entity_api->destroy_child_allocator(ctx, &a);
}

static void create(tm_entity_context_o *entity_ctx)
{
    tm_allocator_i a;
    tm_entity_api->create_child_allocator(entity_ctx, TYPE__THEMACHINERY_TEST_0005_COMPONENT, &a);
    gameplay_component_manager_t *manager = tm_alloc(&a, sizeof(*manager));

    *manager = (gameplay_component_manager_t){
        .allocator = a,
        .entity_ctx = entity_ctx
    };

    const tm_component_i component = {
        .name = TYPE__THEMACHINERY_TEST_0005_COMPONENT,
        .bytes = sizeof(tm_gameplay_context_t),
        .manager = (tm_component_manager_o *)manager,
        .add = (void (*)(tm_component_manager_o *, tm_entity_t, void *))component_added,
        .remove = (void (*)(tm_component_manager_o *, tm_entity_t, void *))component_removed,
        .destroy = (void (*)(tm_component_manager_o *))destroy,
    };

    tm_entity_api->register_component(entity_ctx, &component);
}

static void component_hot_reload(tm_entity_context_o *entity_ctx, tm_component_i *component)
{
    component->add = (void (*)(tm_component_manager_o *, tm_entity_t, void *))component_added;
    component->remove = (void (*)(tm_component_manager_o *, tm_entity_t, void *))component_removed;
    component->destroy = (void (*)(tm_component_manager_o *))destroy;
}

static tm_ci_editor_ui_i editor_aspect = { 0 };

static void create_truth_types(struct tm_the_truth_o *tt)
{
    const uint64_t object_type = tm_the_truth_api->create_object_type(tt, TYPE__THEMACHINERY_TEST_0005_COMPONENT, 0, 0);
    const uint64_t component = tm_the_truth_api->create_object_of_type(tt, tm_the_truth_api->object_type_from_name_hash(tt, TYPE_HASH__THEMACHINERY_TEST_0005_COMPONENT), TM_TT_NO_UNDO_SCOPE);
    (void)component;

    // This is needed in order for the component to show up in the editor.
    tm_the_truth_api->set_aspect(tt, object_type, TM_CI_EDITOR_UI, &editor_aspect);

    tm_string_repository = tm_the_truth_api->string_repository(tt);
}

TM_DLL_EXPORT void tm_load_plugin(struct tm_api_registry_api *reg, bool load)
{
    g = reg->get(TM_GAMEPLAY_API_NAME);
    tm_entity_api = reg->get(TM_ENTITY_API_NAME);
    tm_the_truth_api = reg->get(TM_THE_TRUTH_API_NAME);

    tm_scene_tree_component_api = reg->get(TM_SCENE_TREE_COMPONENT_API_NAME);
    tm_logger_api = reg->get(TM_LOGGER_API_NAME);
    tm_task_system_api = reg->get(TM_TASK_SYSTEM_API_NAME);

    tm_add_or_remove_implementation(reg, load, TM_THE_TRUTH_CREATE_TYPES_INTERFACE_NAME, create_truth_types);
    tm_add_or_remove_implementation(reg, load, TM_ENTITY_CREATE_COMPONENT_INTERFACE_NAME, create);

    static tm_entity_hot_reload_component_i hot_reload_component_i = {
        .name_hash = TYPE_HASH__THEMACHINERY_TEST_0005_COMPONENT,
        .reload = component_hot_reload,
    };
    tm_add_or_remove_implementation(reg, load, TM_ENTITY_HOT_RELOAD_COMPONENT_INTERFACE_NAME, &hot_reload_component_i);

    static tm_entity_hot_reload_system_i hot_reload_system_i = {
        .name_hash = TEST_0005_SYSTEM_NAME_HASH,
        .reload = system_hot_reload,
    };
    tm_add_or_remove_implementation(reg, load, TM_ENTITY_HOT_RELOAD_SYSTEM_INTERFACE_NAME, &hot_reload_system_i);
}