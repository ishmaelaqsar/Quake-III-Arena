#include <gtest/gtest.h>

#include "engine_fixture.hpp"
#include "q_shared.h"
#include "qcommon.h"
#include "bg_public.h"

class CollisionFixture : public q3::test::EngineFixture {
protected:
    void SetUp() override {
        q3::test::EngineFixture::SetUp();
        int checksum = 0;
        CM_LoadMap("", qfalse, &checksum);
    }

    void TearDown() override {
        CM_ClearMap();
    }
};

TEST_F(CollisionFixture, EmptyMapBuildsBoxHull) {
    int checksum = 0;
    CM_LoadMap("", qfalse, &checksum);
    EXPECT_GE(CM_NumInlineModels(), 1);
}

TEST_F(CollisionFixture, BoxTraceFromOutsideHits) {
    vec3_t mins = {-10, -10, -10};
    vec3_t maxs = {10, 10, 10};
    clipHandle_t box = CM_TempBoxModel(mins, maxs, 0);

    trace_t tr;
    vec3_t start = {100, 0, 0};
    vec3_t end = {0, 0, 0};
    vec3_t zero = {0, 0, 0};

    CM_BoxTrace(&tr, start, end, zero, zero, box, MASK_PLAYERSOLID, 0);

    EXPECT_FALSE(tr.startsolid);
    EXPECT_FALSE(tr.allsolid);
    EXPECT_LT(tr.fraction, 1.0f);
    EXPECT_FLOAT_EQ(tr.plane.normal[0], 1.0f);
    EXPECT_FLOAT_EQ(tr.plane.normal[1], 0.0f);
    EXPECT_FLOAT_EQ(tr.plane.normal[2], 0.0f);
}

TEST_F(CollisionFixture, TraceStartingInsideIsStartSolid) {
    vec3_t mins = {-10, -10, -10};
    vec3_t maxs = {10, 10, 10};
    clipHandle_t box = CM_TempBoxModel(mins, maxs, 0);

    trace_t tr;
    vec3_t start = {0, 0, 0};
    vec3_t end = {50, 0, 0};
    vec3_t zero = {0, 0, 0};

    CM_BoxTrace(&tr, start, end, zero, zero, box, MASK_PLAYERSOLID, 0);

    EXPECT_TRUE(tr.startsolid);
}

TEST_F(CollisionFixture, PointContentsInsideBox) {
    vec3_t mins = {-10, -10, -10};
    vec3_t maxs = {10, 10, 10};
    clipHandle_t box = CM_TempBoxModel(mins, maxs, 0);

    vec3_t inside = {0, 0, 0};
    vec3_t outside = {50, 50, 50};

    EXPECT_NE(CM_PointContents(inside, box) & CONTENTS_BODY, 0);
    EXPECT_EQ(CM_PointContents(outside, box) & CONTENTS_BODY, 0);
}

TEST_F(CollisionFixture, TransformedTraceRespectsRotation) {
    vec3_t mins = {-20, -5, -5};
    vec3_t maxs = {20, 5, 5};
    clipHandle_t box = CM_TempBoxModel(mins, maxs, 0);

    vec3_t origin = {0, 0, 0};
    vec3_t angles = {0, 90, 0}; // 90 degree yaw turns X-aligned box into Y-aligned
    trace_t tr;
    vec3_t start = {0, 100, 0};
    vec3_t end = {0, 0, 0};
    vec3_t zero = {0, 0, 0};

    CM_TransformedBoxTrace(&tr, start, end, zero, zero, box, MASK_PLAYERSOLID, origin, angles, 0);

    EXPECT_FALSE(tr.startsolid);
    EXPECT_LT(tr.fraction, 1.0f);
    EXPECT_FLOAT_EQ(tr.plane.normal[1], 1.0f);
}

TEST_F(CollisionFixture, OneBrushBspFixture) {
    GTEST_SKIP() << "One-brush BSP fixture not generated yet";
}
