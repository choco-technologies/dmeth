#define DMOD_ENABLE_REGISTRATION ON
#include "dmod_test.h"
#include "dmeth.h"

static dmeth_t g_handle = NULL;

void dmod_test_setup(void)
{
    g_handle = dmeth_create();
}

void dmod_test_teardown(void)
{
    dmeth_destroy(g_handle);
    g_handle = NULL;
}

DMOD_TEST_STEP(dmeth_create)
{
    DMOD_TEST_EXPECT_NOT_NULL(g_handle);
}

DMOD_TEST_STEP(dmeth_is_valid)
{
    DMOD_TEST_EXPECT_TRUE(dmeth_is_valid(g_handle));
}

DMOD_TEST_STEP(dmeth_destroy_null)
{
    /* Destroying NULL must not crash. */
    dmeth_destroy(NULL);
}
