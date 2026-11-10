// Deep scan: an in-level beam search over every reachable player state.
//
// The real player becomes a noclip "carrier" that drives the level (triggers, move
// actions, dual/mode changes) while a swarm of branch states is stepped against the
// exact same object positions every tick. Each tick every branch is expanded into
// "keep input" and "toggle input" children, children are merged by a quantized state
// key, the beam is pruned with a y-stratified spread, and the carrier follows a
// leader branch. When the scan ends, a backward pass marks every recorded cell as
// viable (a descendant reaches the end), dead (every continuation dies) or unknown,
// and the per-tick cell table is saved per level. While playing, simulate() asks the
// table about ghost states so the buffer judges clicks by the whole level instead of
// a short horizon.
//
// This file is included inside main.cpp's anonymous namespace, after the simulation
// helpers and before simulate().

#if defined(GEODE_IS_WINDOWS)
#define AFTERFLOW_DEEP 1
#else
#define AFTERFLOW_DEEP 0
#endif

namespace deep {

    constexpr char const* KEYBIND_DEEP = "keybind-deep";
    constexpr char const* KEYBIND_DEEP_PAUSE = "keybind-deep-pause";

#if AFTERFLOW_DEEP

    constexpr uint32_t FILE_MAGIC = 0x53444641u;
    constexpr uint32_t FILE_VERSION = 1;
    constexpr uint16_t LINK_DEAD = 0xFFFF;
    constexpr uint16_t LINK_PRUNED = 0xFFFE;
    constexpr uint16_t LINK_WIPE = 0xFFFD;
    constexpr uint16_t LINK_NONE = 0xFFFC;
    constexpr int MAX_BEAM = 0xFFF0;
    constexpr float Y_RES = 2.f;
    constexpr double VY_RES_INV = 2.0;
    constexpr int FRONTIER_TICKS = 240;
    constexpr float X_MATCH = 2.5f;
    constexpr float X_LOOKUP = 8.f;
    constexpr size_t MAX_USED = 24;

    enum : uint8_t { FATE_UNKNOWN = 0, FATE_VIABLE = 1, FATE_DEAD = 2, FATE_FRONTIER = 3 };
    enum : uint8_t { TICK_GAP = 1, TICK_DUAL = 2, TICK_RESEED = 4 };

#define AF_DS_SCALARS(X) \
    X(m_wasTeleported) X(m_fixGravityBug) X(m_reverseSync) X(m_yVelocityBeforeSlope) X(m_dashX) X(m_dashY) \
    X(m_dashAngle) X(m_dashStartTime) X(m_dashRing) X(m_slopeStartTime) X(m_lastCollisionBottom) \
    X(m_lastCollisionTop) X(m_lastCollisionLeft) X(m_lastCollisionRight) X(m_unk50C) X(m_unk510) X(m_currentSlope2) \
    X(m_preLastGroundObject) X(m_slopeAngle) X(m_slopeSlidingMaybeRotated) X(m_quickCheckpointMode) X(m_collidedObject) X(m_lastGroundObject) \
    X(m_collidingWithLeft) X(m_collidingWithRight) X(m_maybeSavedPlayerFrame) X(m_scaleXRelated2) X(m_groundYVelocity) X(m_yVelocityRelated) \
    X(m_scaleXRelated3) X(m_collidingBetweenSteps) X(m_scaleXRelated4) X(m_scaleXRelated5) X(m_isCollidingWithSlope) X(m_isBallRotating) \
    X(m_unk669) X(m_currentPotentialSlope) X(m_currentSlope) X(unk_584) X(m_collidingWithSlopeId) X(m_slopeFlipGravityRelated) \
    X(m_slopeAngleRadians) X(m_rotationSpeed) X(m_rotateSpeed) X(m_isRotating) X(m_isBallRotating2) X(m_speedMultiplier) \
    X(m_yStart) X(m_gravity) X(m_unk648) X(m_gameModeChangedTime) X(m_padRingRelated) X(m_maybeIsFalling) \
    X(m_shouldTryPlacingCheckpoint) X(m_maybeCanRunIntoBlocks) X(m_isOnGround3) X(m_checkpointTimeout) X(m_lastCheckpointTime) X(m_lastJumpTime) \
    X(m_lastFlipTime) X(m_lastSpiderFlipTime) X(m_unkBool5) X(m_accelerationOrSpeed) X(m_snapDistance) X(m_ringJumpRelated) \
    X(m_objectSnappedTo) X(m_onFlyCheckpointTries) X(m_maybeSpriteRelated) X(m_slopeRotation) X(m_currentSlopeYVelocity) X(m_unk3d0) \
    X(m_blackOrbRelated) X(m_unk3e0) X(m_unk3e1) X(m_isAccelerating) X(m_isCurrentSlopeTop) X(m_collidedTopMinY) \
    X(m_collidedBottomMaxY) X(m_collidedLeftMaxX) X(m_collidedRightMinX) X(m_canPlaceCheckpoint) X(m_maybeIsColliding) X(m_jumpBuffered) \
    X(m_stateRingJump) X(m_wasJumpBuffered) X(m_wasRobotJump) X(m_stateJumpBuffered) X(m_stateRingJump2) X(m_touchedRing) \
    X(m_touchedCustomRing) X(m_touchedGravityPortal) X(m_maybeTouchedBreakableBlock) X(m_jumpRelatedAC2) X(m_touchedPad) X(m_yVelocity) \
    X(m_fallSpeed) X(m_isOnSlope) X(m_wasOnSlope) X(m_slopeVelocity) X(m_maybeUpsideDownSlope) X(m_isShip) \
    X(m_isBird) X(m_isBall) X(m_isDart) X(m_isRobot) X(m_isSpider) X(m_isUpsideDown) \
    X(m_isOnGround) X(m_isGoingLeft) X(m_isSideways) X(m_isSwing) X(m_reverseRelated) X(m_maybeReverseSpeed) \
    X(m_maybeReverseAcceleration) X(m_xVelocityRelated2) X(m_isDashing) X(m_dashFireFrame) X(m_groundObjectMaterial) X(m_vehicleSize) \
    X(m_playerSpeed) X(m_shipRotation) X(m_lastPortalPos) X(m_unkUnused3) X(m_isOnGround2) X(m_lastLandTime) \
    X(m_platformerVelocityRelated) X(m_maybeIsBoosted) X(m_scaleXRelatedTime) X(m_decreaseBoostSlide) X(m_unkA29) X(m_isLocked) \
    X(m_controlsDisabled) X(m_lastGroundedPos) X(m_lastActivatedPortal) X(m_hasEverJumped) X(m_hasEverHitRing) X(m_position) \
    X(m_unkA99) X(m_totalTime) X(m_isBeingSpawnedByDualPortal) X(m_audioScale) X(m_unkAngle1) X(m_yVelocityRelated3) \
    X(m_followRelated) X(m_unk838) X(m_stateOnGround) X(m_stateUnk) X(m_stateNoStickX) X(m_stateNoStickY) \
    X(m_stateUnk2) X(m_stateBoostX) X(m_stateBoostY) X(m_maybeStateForce2) X(m_stateScale) X(m_platformerXVelocity) \
    X(m_holdingRight) X(m_holdingLeft) X(m_leftPressedFirst) X(m_scaleXRelated) X(m_maybeHasStopped) X(m_xVelocityRelated) \
    X(m_maybeGoingCorrectSlopeDirection) X(m_isSliding) X(m_maybeSlopeForce) X(m_isOnIce) X(m_physDeltaRelated) X(m_isOnGround4) \
    X(m_maybeSlidingTime) X(m_maybeSlidingStartTime) X(m_changedDirectionsTime) X(m_slopeEndTime) X(m_isMoving) X(m_platformerMovingLeft) \
    X(m_platformerMovingRight) X(m_isSlidingRight) X(m_maybeChangedDirectionAngle) X(m_unkUnused2) X(m_isPlatformer) X(m_stateNoAutoJump) \
    X(m_stateDartSlide) X(m_stateHitHead) X(m_stateFlipGravity) X(m_gravityMod) X(m_stateForce) X(m_stateForceVector) \
    X(m_affectedByForces) X(m_lastMovedTime) X(m_playerSpeedAC) X(m_fixRobotJump) X(m_inputsLocked) X(m_unkUnused) \
    X(m_isOutOfBounds) X(m_fallStartY) X(m_ignoreDamage) X(m_enable22Changes) X(m_enableImpulseFix)

    // m_maybeLastGroundObject is left out on purpose: despite the binding name it is a CCNode each PlayerObject creates and
    // retains in init, copying it between players frees another player's node later (heap corruption)

    // full physics state of one PlayerObject, every member except owned visuals and identity.
    // members the bindings in use do not have become a dummy char and are skipped when copying
    struct PState {
#define AF_DS_DECL(name)         template <class P> static auto typeOf_##name(int) -> std::remove_cvref_t<decltype(std::declval<P&>().name)>;         template <class P> static char typeOf_##name(...);         decltype(typeOf_##name<PlayerObject>(0)) name{};
        AF_DS_SCALARS(AF_DS_DECL)
        AF_DS_DECL(m_rotateObjectsRelated)
        AF_DS_DECL(m_potentialSlopeMap)
        AF_DS_DECL(m_ringRelatedSet)
        AF_DS_DECL(m_touchedRings)
        AF_DS_DECL(m_playerFollowFloats)
        AF_DS_DECL(m_jumpPadRelated)
        AF_DS_DECL(m_holdingButtons)
#undef AF_DS_DECL
        CCPoint nodePos;
        float rotationX = 0.f;
        float rotationY = 0.f;
        float scaleX = 1.f;
        float scaleY = 1.f;
        CCPoint lastPosition;
        float width = 0.f;
        float height = 0.f;
        std::vector<CCObject*> touchingRings;
    };

    template <class Player>
    void saveState(Player* player, PState& out) {
#define AF_DS_SAVE(name)         if constexpr (requires { player->name; }) {             out.name = player->name;         }
        AF_DS_SCALARS(AF_DS_SAVE)
        AF_DS_SAVE(m_rotateObjectsRelated)
        AF_DS_SAVE(m_potentialSlopeMap)
        AF_DS_SAVE(m_ringRelatedSet)
        AF_DS_SAVE(m_touchedRings)
        AF_DS_SAVE(m_playerFollowFloats)
        AF_DS_SAVE(m_jumpPadRelated)
        AF_DS_SAVE(m_holdingButtons)
#undef AF_DS_SAVE
        out.nodePos = player->getPosition();
        out.rotationX = player->getRotationX();
        out.rotationY = player->getRotationY();
        out.scaleX = player->getScaleX();
        out.scaleY = player->getScaleY();
        if constexpr (requires { player->m_lastPosition; }) {
            out.lastPosition = player->m_lastPosition;
        }
        out.width = player->m_width;
        out.height = player->m_height;
        out.touchingRings.clear();
        if (auto* rings = player->m_touchingRings) {
            for (unsigned index = 0; index < rings->count(); ++index) {
                out.touchingRings.push_back(rings->objectAtIndex(index));
            }
        }
    }

    // GD's own mode toggles first (in copyAttributes order, without effects), then every raw field
    template <class Player>
    void loadState(Player* player, PState const& in) {
        if (player->m_isUpsideDown != in.m_isUpsideDown) {
            player->flipGravity(in.m_isUpsideDown, true);
        }
        if (player->m_isGoingLeft != in.m_isGoingLeft) {
            player->doReversePlayer(in.m_isGoingLeft);
        }
        if (player->m_isShip != in.m_isShip) {
            player->toggleFlyMode(in.m_isShip, true);
        }
        if (player->m_isBird != in.m_isBird) {
            player->toggleBirdMode(in.m_isBird, true);
        }
        if (player->m_isBall != in.m_isBall) {
            player->toggleRollMode(in.m_isBall, true);
        }
        if (player->m_isDart != in.m_isDart) {
            player->toggleDartMode(in.m_isDart, true);
        }
        if (player->m_isRobot != in.m_isRobot) {
            player->toggleRobotMode(in.m_isRobot, true);
        }
        if (player->m_isSpider != in.m_isSpider) {
            player->toggleSpiderMode(in.m_isSpider, true);
        }
        if (player->m_isSwing != in.m_isSwing) {
            player->toggleSwingMode(in.m_isSwing, true);
        }
        if (player->m_playerSpeed != in.m_playerSpeed) {
            player->updateTimeMod(in.m_playerSpeed, true);
        }
        if ((player->m_vehicleSize != 1.f) != (in.m_vehicleSize != 1.f)) {
            player->togglePlayerScale(in.m_vehicleSize != 1.f, true);
        }

#define AF_DS_LOAD(name)         if constexpr (requires { player->name; }) {             player->name = in.name;         }
        AF_DS_SCALARS(AF_DS_LOAD)
#undef AF_DS_LOAD
        if constexpr (requires { player->m_rotateObjectsRelated; }) {
            if (!player->m_rotateObjectsRelated.empty() || !in.m_rotateObjectsRelated.empty()) {
                player->m_rotateObjectsRelated = in.m_rotateObjectsRelated;
            }
        }
        if constexpr (requires { player->m_potentialSlopeMap; }) {
            if (!player->m_potentialSlopeMap.empty() || !in.m_potentialSlopeMap.empty()) {
                player->m_potentialSlopeMap = in.m_potentialSlopeMap;
            }
        }
        if constexpr (requires { player->m_ringRelatedSet; }) {
            if (player->m_ringRelatedSet != in.m_ringRelatedSet) {
                player->m_ringRelatedSet = in.m_ringRelatedSet;
            }
        }
        if constexpr (requires { player->m_touchedRings; }) {
            if (player->m_touchedRings != in.m_touchedRings) {
                player->m_touchedRings = in.m_touchedRings;
            }
        }
        if constexpr (requires { player->m_playerFollowFloats; }) {
            if (player->m_playerFollowFloats != in.m_playerFollowFloats) {
                player->m_playerFollowFloats = in.m_playerFollowFloats;
            }
        }
        if constexpr (requires { player->m_jumpPadRelated; }) {
            if (player->m_jumpPadRelated != in.m_jumpPadRelated) {
                player->m_jumpPadRelated = in.m_jumpPadRelated;
            }
        }
        if (player->m_holdingButtons != in.m_holdingButtons) {
            player->m_holdingButtons = in.m_holdingButtons;
        }

        player->setPosition(in.nodePos);
        player->setRotationX(in.rotationX);
        player->setRotationY(in.rotationY);
        player->setScaleX(in.scaleX);
        player->setScaleY(in.scaleY);
        if constexpr (requires { player->m_lastPosition; }) {
            player->m_lastPosition = in.lastPosition;
        }
        player->m_width = in.width;
        player->m_height = in.height;
        player->m_isObjectRectDirty = true;
        if constexpr (requires { player->m_isOrientedBoxDirty; }) {
            player->m_isOrientedBoxDirty = true;
        }

        if (auto* rings = player->m_touchingRings) {
            bool same = rings->count() == in.touchingRings.size();
            for (unsigned index = 0; same && index < rings->count(); ++index) {
                same = rings->objectAtIndex(index) == in.touchingRings[index];
            }
            if (!same) {
                rings->removeAllObjects();
                for (auto* ring : in.touchingRings) {
                    rings->addObject(ring);
                }
            }
        }
    }

    // members of the list the compiled bindings do not have (they are not copied between branches)
    template <class Player>
    std::vector<char const*> missingFields() {
        std::vector<char const*> missing;
#define AF_DS_MISSING(name)         if constexpr (!requires(Player* player) { player->name; }) {             missing.push_back(#name);         }
        AF_DS_SCALARS(AF_DS_MISSING)
#undef AF_DS_MISSING
        return missing;
    }

    bool holdsJump(PlayerObject* player) {
        auto found = player->m_holdingButtons.find(static_cast<int>(PlayerButton::Jump));
        return found != player->m_holdingButtons.end() && found->second;
    }

    // GJBaseGameLayer::flipGravity for ghosts: the player flips, and in dual GD flips the partner the other way when both share a mode
    void ghostFlipGravity(GJBaseGameLayer* layer, PlayerObject* player, bool flip) {
        if (player->m_isUpsideDown == flip) {
            return;
        }
        player->flipGravity(flip, true);
        if (!g_deepStep.stepping || !g_deepStep.dual || !layer) {
            return;
        }
        PlayerObject* partner = player == g_deepWorker1 ? g_deepWorker2 : player == g_deepWorker2 ? g_deepWorker1 : nullptr;
        if (!partner) {
            return;
        }
        // raw 2.2081 windows offsets read by GJBaseGameLayer::flipGravity (0x212b00): an unlink flag in the game state,
        // m_gameState.m_isDualMode and the level settings' unlink option
        auto const* base = reinterpret_cast<uint8_t const*>(layer);
        if (base[0x860] != 0 || base[0x422] == 0) {
            return;
        }
        auto const* settings = *reinterpret_cast<uint8_t const* const*>(base + 0xdb0);
        if (settings && settings[0x154] != 0) {
            return;
        }
        if (partner->m_isShip != player->m_isShip || partner->m_isBall != player->m_isBall || partner->m_isBird != player->m_isBird ||
            partner->m_isSpider != player->m_isSpider || partner->m_isRobot != player->m_isRobot || partner->m_isSwing != player->m_isSwing) {
            return;
        }
        partner->flipGravity(!flip, true);
    }

    void resetLogs(PlayerObject* player) {
        player->m_collisionLogTop->removeAllObjects();
        player->m_collisionLogBottom->removeAllObjects();
        player->m_collisionLogLeft->removeAllObjects();
        player->m_collisionLogRight->removeAllObjects();
    }

    // ------------------------------------------------------------------ keys

    uint64_t splitmix(uint64_t value) {
        value += 0x9E3779B97F4A7C15ull;
        value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ull;
        value = (value ^ (value >> 27)) * 0x94D049BB133111EBull;
        return value ^ (value >> 31);
    }

    int speedIndex(float speed) {
        static constexpr float SPEEDS[] = { 0.7f, 0.9f, 1.1f, 1.3f, 1.6f };
        int best = 1;
        float bestDistance = 1e9f;
        for (int index = 0; index < 5; ++index) {
            float distance = std::abs(SPEEDS[index] - speed);
            if (distance < bestDistance) {
                bestDistance = distance;
                best = index;
            }
        }
        return best;
    }

    int modeIndex(bool ship, bool ball, bool bird, bool dart, bool robot, bool spider, bool swing) {
        if (ship) { return 1; }
        if (ball) { return 2; }
        if (bird) { return 3; }
        if (dart) { return 4; }
        if (robot) { return 5; }
        if (spider) { return 6; }
        if (swing) { return 7; }
        return 0;
    }

    char const* modeName(int mode) {
        static char const* const NAMES[] = { "cube", "ship", "ball", "ufo", "wave", "robot", "spider", "swing" };
        return mode >= 0 && mode < 8 ? NAMES[mode] : "?";
    }

    struct Quant {
        uint32_t flags = 0;
        int64_t y = 0;
        int64_t vy = 0;
    };

    template <class Source>
    Quant quantFields(Source const& source, float y) {
        Quant quant;
        int mode = modeIndex(source.m_isShip, source.m_isBall, source.m_isBird, source.m_isDart, source.m_isRobot, source.m_isSpider, source.m_isSwing);
        quant.flags = static_cast<uint32_t>(mode) | (source.m_isUpsideDown ? 1u << 3 : 0u) | (source.m_vehicleSize < 1.f ? 1u << 4 : 0u) |
            (source.m_isOnGround ? 1u << 5 : 0u) | (source.m_isDashing ? 1u << 6 : 0u) | (source.m_isGoingLeft ? 1u << 7 : 0u) |
            (source.m_isSideways ? 1u << 8 : 0u) | (static_cast<uint32_t>(speedIndex(source.m_playerSpeed)) << 9);
        quant.y = static_cast<int64_t>(std::floor(y / Y_RES));
        quant.vy = static_cast<int64_t>(std::floor(source.m_yVelocity * VY_RES_INV));
        return quant;
    }

    Quant quantOf(PlayerObject* player) {
        return quantFields(*player, player->getPositionY());
    }

    Quant quantOf(PState const& state) {
        return quantFields(state, state.nodePos.y);
    }

    uint64_t keyOf(Quant const& first, Quant const* second, bool held) {
        uint64_t hash = splitmix(first.flags | (held ? 1ull << 40 : 0ull) | (second ? 1ull << 41 : 0ull));
        hash = splitmix(hash ^ static_cast<uint64_t>(first.y));
        hash = splitmix(hash ^ (static_cast<uint64_t>(first.vy) * 0x100000001B3ull));
        if (second) {
            hash = splitmix(hash ^ second->flags);
            hash = splitmix(hash ^ static_cast<uint64_t>(second->y) * 31ull);
            hash = splitmix(hash ^ (static_cast<uint64_t>(second->vy) * 0x100000001B3ull));
        }
        return hash | 1ull;
    }

    uint64_t fnv64(std::string_view text) {
        uint64_t hash = 0xcbf29ce484222325ull;
        for (unsigned char character : text) {
            hash ^= character;
            hash *= 0x100000001b3ull;
        }
        return hash;
    }

    // ------------------------------------------------------------------ table (runtime data)

    struct Table {
        int levelID = 0;
        uint64_t levelHash = 0;
        bool complete = false;
        std::vector<float> xs;
        std::vector<uint32_t> begin;
        std::vector<uint64_t> keys;
        std::vector<uint8_t> labels;
        size_t viableCount = 0;
        size_t deadCount = 0;

        int ticks() const {
            return static_cast<int>(xs.size());
        }

        int find(int tick, uint64_t key) const {
            if (tick < 0 || tick >= ticks()) {
                return 0;
            }
            auto first = keys.begin() + begin[tick];
            auto last = keys.begin() + begin[tick + 1];
            auto found = std::lower_bound(first, last, key);
            if (found == last || *found != key) {
                return 0;
            }
            return labels[found - keys.begin()] == FATE_VIABLE ? 1 : -1;
        }

        bool xMatches(int tick, float x, float tolerance) const {
            return tick >= 0 && tick < ticks() && std::abs(xs[tick] - x) <= tolerance;
        }

        int findTick(float x, int hint) const {
            int best = -1;
            float bestScore = 1e30f;
            auto consider = [&](int tick) {
                float distance = std::abs(xs[tick] - x);
                if (distance > X_MATCH) {
                    return;
                }
                float score = distance + (hint >= 0 ? std::abs(tick - hint) * 0.01f : 0.f);
                if (score < bestScore) {
                    bestScore = score;
                    best = tick;
                }
            };
            if (hint >= 0) {
                int low = std::max(0, hint - 720);
                int high = std::min(ticks() - 1, hint + 720);
                for (int tick = low; tick <= high; ++tick) {
                    consider(tick);
                }
                if (best >= 0) {
                    return best;
                }
            }
            for (int tick = 0; tick < ticks(); ++tick) {
                consider(tick);
            }
            return best;
        }
    };

    std::filesystem::path dataDirectory() {
        return Mod::get()->getSaveDir() / "deepscan";
    }

    std::filesystem::path dataPath(int levelID, uint64_t levelHash) {
        return dataDirectory() / fmt::format("{}-{:016x}.afds", levelID, levelHash);
    }

    template <class T>
    void writeRaw(std::ofstream& stream, T const& value) {
        stream.write(reinterpret_cast<char const*>(&value), sizeof(T));
    }

    template <class T>
    bool readRaw(std::ifstream& stream, T& value) {
        stream.read(reinterpret_cast<char*>(&value), sizeof(T));
        return static_cast<bool>(stream);
    }

    template <class T>
    void writeVector(std::ofstream& stream, std::vector<T> const& values) {
        uint64_t count = values.size();
        writeRaw(stream, count);
        if (count) {
            stream.write(reinterpret_cast<char const*>(values.data()), static_cast<std::streamsize>(count * sizeof(T)));
        }
    }

    template <class T>
    bool readVector(std::ifstream& stream, std::vector<T>& values, uint64_t limit) {
        uint64_t count = 0;
        if (!readRaw(stream, count) || count > limit) {
            return false;
        }
        values.resize(static_cast<size_t>(count));
        if (count) {
            stream.read(reinterpret_cast<char*>(values.data()), static_cast<std::streamsize>(count * sizeof(T)));
        }
        return static_cast<bool>(stream);
    }

    bool saveTable(Table const& table, std::filesystem::path const& path) {
        std::error_code errorCode;
        std::filesystem::create_directories(path.parent_path(), errorCode);
        auto temporary = path;
        temporary += ".tmp";
        {
            std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
            if (!stream) {
                return false;
            }
            writeRaw(stream, FILE_MAGIC);
            writeRaw(stream, FILE_VERSION);
            writeRaw(stream, table.levelID);
            writeRaw(stream, table.levelHash);
            uint8_t complete = table.complete ? 1 : 0;
            writeRaw(stream, complete);
            writeVector(stream, table.xs);
            writeVector(stream, table.begin);
            writeVector(stream, table.keys);
            writeVector(stream, table.labels);
            if (!stream) {
                return false;
            }
        }
        std::filesystem::rename(temporary, path, errorCode);
        if (errorCode) {
            std::filesystem::remove(path, errorCode);
            std::filesystem::rename(temporary, path, errorCode);
        }
        return !errorCode;
    }

    std::unique_ptr<Table> loadTable(std::filesystem::path const& path) {
        std::ifstream stream(path, std::ios::binary);
        if (!stream) {
            return nullptr;
        }
        uint32_t magic = 0;
        uint32_t version = 0;
        auto table = std::make_unique<Table>();
        uint8_t complete = 0;
        if (!readRaw(stream, magic) || magic != FILE_MAGIC || !readRaw(stream, version) || version != FILE_VERSION) {
            return nullptr;
        }
        if (!readRaw(stream, table->levelID) || !readRaw(stream, table->levelHash) || !readRaw(stream, complete)) {
            return nullptr;
        }
        table->complete = complete != 0;
        constexpr uint64_t LIMIT = 1ull << 31;
        if (!readVector(stream, table->xs, LIMIT) || !readVector(stream, table->begin, LIMIT) || !readVector(stream, table->keys, LIMIT) ||
            !readVector(stream, table->labels, LIMIT)) {
            return nullptr;
        }
        if (table->begin.size() != table->xs.size() + 1 || table->keys.size() != table->labels.size() ||
            (!table->begin.empty() && table->begin.back() != table->keys.size())) {
            return nullptr;
        }
        for (auto label : table->labels) {
            (label == FATE_VIABLE ? table->viableCount : table->deadCount)++;
        }
        return table;
    }

    // ------------------------------------------------------------------ runtime

    struct Runtime {
        std::shared_ptr<Table> table;
        int levelID = 0;
        uint64_t levelHash = 0;
        int rtTick = 0;
        bool synced = false;
        bool useEnabled = true;
        int alignMargin = 1;
        uint64_t lookups = 0;
        uint64_t hits = 0;
    } runtime;

    void log(std::string const& message) {
        say("deep: {}", message);
        geode::log::info("[deep] {}", message);
    }

    // ------------------------------------------------------------------ analysis state

    struct Node {
        PState first;
        PState second;
        bool dual = false;
        bool held = false;
        uint64_t key = 0;
        int parent = -1;
        bool parentInput = false;
        std::vector<GameObject*> used;
        std::vector<GameObject*> orbs;
    };

    struct TickRecord {
        float carrierX = 0.f;
        float carrierY = 0.f;
        uint32_t cellBegin = 0;
        uint32_t cellCount = 0;
        uint8_t flags = 0;
        uint8_t mode = 0;
    };

    struct Pristine {
        GameObject* gameObject = nullptr;
        EffectGameObject* effectObject = nullptr;
        bool isActivated = false;
        bool activated = false;
        bool byPlayer1 = false;
        bool byPlayer2 = false;
        bool savedIsActivated = false;
        bool savedActivated = false;
        bool savedByPlayer1 = false;
        bool savedByPlayer2 = false;
        float x = 0.f;
    };

    class DeepHud;

    struct Analysis {
        bool active = false;
        bool paused = false;
        bool pendingFinish = false;
        bool reachedEnd = false;
        bool abort = false;
        bool suppressAttempts = false;
        bool wasDual = false;
        bool finishing = false;
        int speedMode = 2;
        int beamWidth = 768;
        double budgetMs = 18.0;
        bool swarm = true;
        PlayLayer* layer = nullptr;

        std::vector<Node> current;
        std::vector<Node> next;
        size_t currentCount = 0;
        size_t nextCount = 0;
        std::unordered_map<uint64_t, int> childMap;

        std::vector<TickRecord> ticks;
        std::vector<uint64_t> keys;
        std::vector<uint16_t> link0;
        std::vector<uint16_t> link1;

        int leader = 0;
        uint64_t carrierUpdates = 0;
        uint64_t lastStepped = 0;
        uint64_t updatesAtFrameStart = 0;
        int stepsPerFrame = 8;

        uint64_t expansions = 0;
        uint64_t deaths = 0;
        int gaps = 0;
        int parityBreaks = 0;
        double parityWorst = 0.0;
        int syncs = 0;
        int lastDeaths = 0;
        double lastStepMs = 0.0;
        double frameMs = 0.0;
        double ticksPerSecond = 0.0;
        std::chrono::steady_clock::time_point startTime;
        std::chrono::steady_clock::time_point rateTime;
        uint64_t rateTicks = 0;
        int lastProgressDecile = -1;
        int frameCounter = 0;
        float startX = 0.f;
        float levelLength = 1.f;
        int levelID = 0;
        uint64_t levelHash = 0;
        int parityLogged = 0;

        std::vector<Pristine> pristine;
        std::unordered_set<GameObject*> pristineSeen;

        CCNode* vizRoot = nullptr;
        CCDrawNode* vizChunk = nullptr;
        std::deque<CCDrawNode*> vizChunks;
        int vizChunkTick = 0;
        std::vector<CCPoint> deathPoints;

        DeepHud* hud = nullptr;
        std::string resultText;
    } analysis;

    CCLayer* workerLayer = nullptr;

    bool isWorker(PlayerObject* player) {
        return player && (player == g_deepWorker1 || player == g_deepWorker2);
    }

    PlayerObject* makeWorker(PlayLayer* layer, bool second) {
        if (!workerLayer) {
            workerLayer = CCLayer::create();
            workerLayer->retain();
        }
        auto* worker = PlayerObject::create(1, 1, layer, workerLayer, true);
        if (!worker) {
            return nullptr;
        }
        worker->retain();
        worker->setVisible(false);
        worker->m_playEffects = false;
        [second](auto* target) {
            if constexpr (requires { target->m_isSecondPlayer; }) {
                target->m_isSecondPlayer = second;
            }
        }(worker);
        worker->unscheduleUpdate();
        worker->stopAllActions();
        workerLayer->addChild(worker);
        return worker;
    }

    void destroyWorkers() {
        for (auto** slot : { &g_deepWorker1, &g_deepWorker2 }) {
            if (*slot) {
                (*slot)->removeFromParentAndCleanup(true);
                (*slot)->release();
                *slot = nullptr;
            }
        }
        if (workerLayer) {
            workerLayer->removeAllChildrenWithCleanup(true);
            workerLayer->release();
            workerLayer = nullptr;
        }
    }

    bool layerIsDual(PlayLayer* layer) {
        bool twoPlayer = layer->m_level && layer->m_level->m_twoPlayerMode;
        return !twoPlayer && layer->m_gameState.m_isDualMode && layer->m_player2;
    }

    int levelIdOf(PlayLayer* layer) {
        if (!layer || !layer->m_level) {
            return 0;
        }
        return layer->m_level->m_levelID.value();
    }

    uint64_t levelHashOf(PlayLayer* layer) {
        if (!layer || !layer->m_level) {
            return 0;
        }
        std::string_view levelString(layer->m_level->m_levelString.c_str(), layer->m_level->m_levelString.size());
        return fnv64(levelString);
    }

    int runtimeBase() {
        if (!runtime.table || !runtime.useEnabled || !runtime.synced || analysis.active) {
            return -1;
        }
        return state.isInRealUpdate ? runtime.rtTick : runtime.rtTick + 1;
    }

    // 1 viable, -1 dead, 0 unknown
    int label(int tick, PlayerObject* first, PlayerObject* second, bool dual, bool held) {
        auto const* table = runtime.table.get();
        if (!table || tick < 0 || tick >= table->ticks() || !first) {
            return 0;
        }
        if (std::abs(table->xs[tick] - first->getPositionX()) > X_LOOKUP) {
            return 0;
        }
        ++runtime.lookups;
        Quant quantFirst = quantOf(first);
        Quant quantSecond;
        Quant const* secondPointer = nullptr;
        if (dual && second) {
            quantSecond = quantOf(second);
            secondPointer = &quantSecond;
        }
        int exact = table->find(tick, keyOf(quantFirst, secondPointer, held));
        if (exact != 0) {
            ++runtime.hits;
            return exact;
        }
        int viable = 0;
        int dead = 0;
        for (int deltaY = -1; deltaY <= 1; ++deltaY) {
            for (int deltaV = -1; deltaV <= 1; ++deltaV) {
                if (deltaY == 0 && deltaV == 0) {
                    continue;
                }
                Quant neighbour = quantFirst;
                neighbour.y += deltaY;
                neighbour.vy += deltaV;
                int found = table->find(tick, keyOf(neighbour, secondPointer, held));
                viable += found > 0;
                dead += found < 0;
            }
        }
        if (viable && !dead) {
            ++runtime.hits;
            return 1;
        }
        if (dead && !viable) {
            ++runtime.hits;
            return -1;
        }
        return 0;
    }

    void readSettings() {
        auto* mod = Mod::get();
        analysis.beamWidth = static_cast<int>(std::clamp<int64_t>(mod->getSettingValue<int64_t>("deep-beam"), 16, 4000));
        analysis.budgetMs = std::clamp(mod->getSettingValue<double>("deep-budget"), 2.0, 200.0);
        analysis.swarm = mod->getSettingValue<bool>("deep-swarm");
        runtime.useEnabled = mod->getSettingValue<bool>("deep-use");
        runtime.alignMargin = static_cast<int>(std::clamp<int64_t>(mod->getSettingValue<int64_t>("deep-align"), 0, 6));
    }

    void onLevelInit(PlayLayer* layer) {
        destroyWorkers();
        readSettings();
        runtime.table.reset();
        runtime.synced = false;
        runtime.rtTick = 0;
        runtime.lookups = 0;
        runtime.hits = 0;
        runtime.levelID = levelIdOf(layer);
        runtime.levelHash = levelHashOf(layer);
        auto path = dataPath(runtime.levelID, runtime.levelHash);
        std::error_code errorCode;
        if (std::filesystem::exists(path, errorCode)) {
            runtime.table = loadTable(path);
            if (runtime.table) {
                log(fmt::format("map loaded: {} ticks, {} viable / {} dead cells{}", runtime.table->ticks(), runtime.table->viableCount,
                    runtime.table->deadCount, runtime.table->complete ? "" : " (partial)"));
            }
            else {
                log("map file is unreadable, ignoring it");
            }
        }
    }

    void onResetLevel() {
        if (runtime.lookups >= 64) {
            log(fmt::format("map used: {} lookups, {:.0f}% known", runtime.lookups, 100.0 * static_cast<double>(runtime.hits) / static_cast<double>(runtime.lookups)));
        }
        runtime.lookups = 0;
        runtime.hits = 0;
        runtime.synced = false;
        runtime.rtTick = 0;
    }

    // ------------------------------------------------------------------ pristine object flags

    void recordCarrierCheck(EffectGameObject* effectObject) {
        if (!analysis.active || !effectObject || analysis.pristineSeen.contains(effectObject)) {
            return;
        }
        analysis.pristineSeen.insert(effectObject);
        Pristine entry;
        entry.gameObject = effectObject;
        entry.effectObject = effectObject;
        entry.isActivated = effectObject->m_isActivated;
        entry.activated = effectObject->m_activated;
        entry.byPlayer1 = effectObject->m_activatedByPlayer1;
        entry.byPlayer2 = effectObject->m_activatedByPlayer2;
        entry.x = effectObject->getPositionX();
        analysis.pristine.push_back(entry);
    }

    void swapPristine(bool toPristine, float carrierX) {
        if (toPristine) {
            size_t write = 0;
            for (size_t index = 0; index < analysis.pristine.size(); ++index) {
                auto entry = analysis.pristine[index];
                if (entry.x < carrierX - 400.f) {
                    continue;
                }
                analysis.pristine[write++] = entry;
            }
            analysis.pristine.resize(write);
            for (auto& entry : analysis.pristine) {
                entry.savedIsActivated = entry.gameObject->m_isActivated;
                entry.savedActivated = entry.effectObject->m_activated;
                entry.savedByPlayer1 = entry.effectObject->m_activatedByPlayer1;
                entry.savedByPlayer2 = entry.effectObject->m_activatedByPlayer2;
                entry.gameObject->m_isActivated = entry.isActivated;
                entry.effectObject->m_activated = entry.activated;
                entry.effectObject->m_activatedByPlayer1 = entry.byPlayer1;
                entry.effectObject->m_activatedByPlayer2 = entry.byPlayer2;
            }
            return;
        }
        for (auto& entry : analysis.pristine) {
            entry.gameObject->m_isActivated = entry.savedIsActivated;
            entry.effectObject->m_activated = entry.savedActivated;
            entry.effectObject->m_activatedByPlayer1 = entry.savedByPlayer1;
            entry.effectObject->m_activatedByPlayer2 = entry.savedByPlayer2;
        }
    }

    // ------------------------------------------------------------------ visualisation

    void clearViz() {
        if (analysis.vizRoot) {
            analysis.vizRoot->removeFromParentAndCleanup(true);
            analysis.vizRoot->release();
        }
        analysis.vizRoot = nullptr;
        analysis.vizChunk = nullptr;
        analysis.vizChunks.clear();
    }

    void createViz(PlayLayer* layer) {
        clearViz();
        if (!analysis.swarm || !layer->m_player1) {
            return;
        }
        CCNode* parent = layer->m_player1->getParent();
        if (!parent || typeinfo_cast<CCSpriteBatchNode*>(parent)) {
            parent = layer->m_objectLayer;
        }
        if (!parent) {
            return;
        }
        analysis.vizRoot = CCNode::create();
        analysis.vizRoot->retain();
        parent->addChild(analysis.vizRoot, 9999);
    }

    CCDrawNode* vizNode(int tick) {
        if (!analysis.vizRoot) {
            return nullptr;
        }
        if (!analysis.vizChunk || tick - analysis.vizChunkTick >= 480) {
            analysis.vizChunk = makeDrawNode();
            if (!analysis.vizChunk) {
                return nullptr;
            }
            analysis.vizRoot->addChild(analysis.vizChunk);
            analysis.vizChunks.push_back(analysis.vizChunk);
            analysis.vizChunkTick = tick;
            while (analysis.vizChunks.size() > 4) {
                analysis.vizChunks.front()->removeFromParentAndCleanup(true);
                analysis.vizChunks.pop_front();
            }
        }
        return analysis.vizChunk;
    }

    void drawTick(int tick) {
        if (!analysis.vizRoot) {
            return;
        }
        auto* node = vizNode(tick);
        if (!node) {
            return;
        }
        ccColor3B accent = accentColor();
        if (tick % 2 == 0 && analysis.nextCount > 0) {
            size_t count = analysis.nextCount;
            size_t samples = std::min<size_t>(count, 40);
            ccColor4F swarm = color4(accent, 120);
            for (size_t sample = 0; sample < samples; ++sample) {
                auto const& candidate = analysis.next[sample * count / samples];
                node->drawDot(candidate.first.nodePos, 1.1f, swarm);
                if (candidate.dual) {
                    node->drawDot(candidate.second.nodePos, 1.1f, swarm);
                }
            }
        }
        ccColor4F deathColor = color4(255, 70, 70, 210);
        for (size_t index = 0; index < analysis.deathPoints.size() && index < 6; ++index) {
            node->drawDot(analysis.deathPoints[index * analysis.deathPoints.size() / std::min<size_t>(analysis.deathPoints.size(), 6)], 1.4f, deathColor);
        }
        if (analysis.nextCount > 0 && analysis.leader >= 0 && static_cast<size_t>(analysis.leader) < analysis.nextCount) {
            auto const& lead = analysis.next[analysis.leader];
            node->drawDot(lead.first.nodePos, 1.5f, color4(255, 255, 255, 235));
            if (lead.dual) {
                node->drawDot(lead.second.nodePos, 1.5f, color4(255, 255, 255, 235));
            }
        }
    }

    // ------------------------------------------------------------------ HUD

    void requestStop(bool abortScan);
    void togglePause();
    void setSpeed(int mode);

    class DeepHud : public CCLayer {
    public:
        static DeepHud* create() {
            auto* hud = new DeepHud();
            if (hud && hud->init()) {
                hud->autorelease();
                return hud;
            }
            delete hud;
            return nullptr;
        }

        bool init() override {
            if (!CCLayer::init()) {
                return false;
            }
            setTouchMode(kCCTouchesOneByOne);
            setTouchPriority(-900);
            setTouchEnabled(true);
            m_frame = makeDrawNode();
            if (m_frame) {
                addChild(m_frame, 0);
            }
            ccColor3B text = getUITextColor();
            m_title = hudLabel("DEEP SCAN", true, 0.55f, accentColor());
            m_status = hudLabel("", false, 0.7f, text);
            for (auto*& line : m_lines) {
                line = hudLabel("", false, 0.68f, softTextColor());
                if (line) {
                    line->setAnchorPoint(CCPoint(0.f, 0.5f));
                    addChild(line, 2);
                }
            }
            for (int index = 0; index < BUTTON_COUNT; ++index) {
                m_buttonLabels[index] = hudLabel(BUTTON_TEXT[index], false, 0.62f, text);
                if (m_buttonLabels[index]) {
                    addChild(m_buttonLabels[index], 3);
                }
            }
            if (m_title) {
                m_title->setAnchorPoint(CCPoint(0.f, 0.5f));
                addChild(m_title, 2);
            }
            if (m_status) {
                m_status->setAnchorPoint(CCPoint(1.f, 0.5f));
                addChild(m_status, 2);
            }
            layoutHud();
            scheduleUpdate();
            return true;
        }

        void registerWithTouchDispatcher() override {
            CCTouchDispatcher::get()->addTargetedDelegate(this, -900, true);
        }

        bool ccTouchBegan(CCTouch* touch, CCEvent*) override {
            if (!isVisible()) {
                return false;
            }
            CCPoint point = convertToNodeSpace(touch->getLocation());
            if (!m_panel.containsPoint(point)) {
                return false;
            }
            for (int index = 0; index < BUTTON_COUNT; ++index) {
                if (m_buttons[index].containsPoint(point)) {
                    press(index);
                    break;
                }
            }
            return true;
        }

        void update(float deltaTime) override {
            m_refreshTimer += deltaTime;
            if (m_refreshTimer >= 0.1f) {
                m_refreshTimer = 0.f;
                refresh();
            }
            if (m_fadeTimer > 0.f) {
                m_fadeTimer -= deltaTime;
                if (m_fadeTimer <= 0.f) {
                    // removing itself from its own update: keep it alive until the frame ends
                    retain();
                    removeFromParentAndCleanup(true);
                    autorelease();
                }
            }
        }

        void finishLater(float seconds) {
            m_fadeTimer = seconds;
        }

        void refresh();

    private:
        static constexpr int BUTTON_COUNT = 5;
        static constexpr int LINE_COUNT = 5;
        static constexpr char const* BUTTON_TEXT[BUTTON_COUNT] = { "PAUSE", "1X", "4X", "MAX", "STOP" };

        CCDrawNode* m_frame = nullptr;
        CCLabelBMFont* m_title = nullptr;
        CCLabelBMFont* m_status = nullptr;
        CCLabelBMFont* m_lines[LINE_COUNT] = {};
        CCLabelBMFont* m_buttonLabels[BUTTON_COUNT] = {};
        CCRect m_panel;
        CCRect m_buttons[BUTTON_COUNT];
        CCRect m_bar;
        float m_refreshTimer = 0.f;
        float m_fadeTimer = 0.f;

        // fixed sizes: the panel font scale setting would push these lines out of the box
        static CCLabelBMFont* hudLabel(char const* text, bool isDisplay, float scale, ccColor3B color) {
            auto* label = CCLabelBMFont::create(text, isDisplay ? displayFont() : bodyFont());
            if (!label) {
                return nullptr;
            }
            label->setScale(scale);
            label->setColor(color);
            if (auto* texture = label->getTexture()) {
                texture->setAntiAliasTexParameters();
            }
            return label;
        }

        void press(int index) {
            switch (index) {
                case 0: togglePause(); break;
                case 1: setSpeed(0); break;
                case 2: setSpeed(1); break;
                case 3: setSpeed(2); break;
                case 4: requestStop(false); break;
                default: break;
            }
            refresh();
        }

        void layoutHud() {
            auto winSize = CCDirector::get()->getWinSize();
            float width = 250.f;
            float height = 94.f;
            float left = 6.f;
            float top = winSize.height - 6.f;
            m_panel = CCRect(left, top - height, width, height);
            float lineY = top - 8.f;
            if (m_title) {
                m_title->setPosition(CCPoint(left + 6.f, lineY));
            }
            if (m_status) {
                m_status->setPosition(CCPoint(left + width - 6.f, lineY));
            }
            float y = top - 23.f;
            for (auto* line : m_lines) {
                if (line) {
                    line->setPosition(CCPoint(left + 6.f, y));
                }
                y -= 9.5f;
            }
            m_bar = CCRect(left + 6.f, top - 73.f, width - 12.f, 2.5f);
            float buttonWidth = (width - 12.f - 4.f * 4.f) / BUTTON_COUNT;
            for (int index = 0; index < BUTTON_COUNT; ++index) {
                m_buttons[index] = CCRect(left + 6.f + index * (buttonWidth + 4.f), top - height + 4.f, buttonWidth, 12.f);
                if (m_buttonLabels[index]) {
                    m_buttonLabels[index]->setPosition(CCPoint(m_buttons[index].getMidX(), m_buttons[index].getMidY()));
                }
            }
        }

        void drawFrame(float progress) {
            if (!m_frame) {
                return;
            }
            m_frame->clear();
            ccColor3B surface = getUISurfaceColor();
            ccColor3B accent = accentColor();
            ccColor4F fill = color4(surface, 238);
            ccColor4F border = color4(accent, 230);
            ccColor4F none = color4(0, 0, 0, 0);
            auto rect = [&](CCRect const& area, ccColor4F inner, ccColor4F edge) {
                CCPoint corners[4] = {
                    CCPoint(area.getMinX(), area.getMinY()), CCPoint(area.getMaxX(), area.getMinY()),
                    CCPoint(area.getMaxX(), area.getMaxY()), CCPoint(area.getMinX(), area.getMaxY())
                };
                m_frame->drawPolygon(corners, 4, inner, 0.5f, edge);
            };
            rect(m_panel, fill, border);
            rect(CCRect(m_panel.getMinX(), m_panel.getMaxY() - 15.f, m_panel.size.width, 0.6f), border, none);
            rect(m_bar, color4(255, 255, 255, 28), none);
            if (progress > 0.f) {
                rect(CCRect(m_bar.origin.x, m_bar.origin.y, m_bar.size.width * std::clamp(progress, 0.f, 1.f), m_bar.size.height), color4(accent, 255), none);
            }
            for (int index = 0; index < BUTTON_COUNT; ++index) {
                bool lit = (index == 0 && analysis.paused) || (index >= 1 && index <= 3 && analysis.speedMode == index - 1);
                rect(m_buttons[index], lit ? color4(accent, 90) : color4(255, 255, 255, 16), color4(accent, lit ? 255 : 110));
            }
        }
    };

    std::string clockText(double seconds) {
        if (!(seconds >= 0.0) || seconds > 359999.0) {
            return "--:--";
        }
        int total = static_cast<int>(seconds);
        return fmt::format("{:02}:{:02}", total / 60, total % 60);
    }

    std::string countText(uint64_t value) {
        if (value >= 10000000ull) {
            return fmt::format("{:.1f}m", value / 1e6);
        }
        if (value >= 10000ull) {
            return fmt::format("{:.1f}k", value / 1e3);
        }
        return fmt::format("{}", value);
    }

    void DeepHud::refresh() {
        auto& scan = analysis;
        float progress = 0.f;
        if (scan.active && !scan.ticks.empty()) {
            float span = std::max(1.f, scan.levelLength - scan.startX);
            progress = std::clamp((scan.ticks.back().carrierX - scan.startX) / span, 0.f, 1.f);
        }
        else if (!scan.active) {
            progress = 1.f;
        }
        if (m_status) {
            m_status->setString(scan.finishing ? "SAVING" : !scan.active ? "DONE" : scan.paused ? "PAUSED" : "RUN");
            m_status->setColor(scan.paused || scan.finishing ? warningColor() : accentColor());
        }
        double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - scan.startTime).count();
        std::string lines[LINE_COUNT];
        if (scan.active) {
            int tick = scan.ticks.empty() ? 0 : static_cast<int>(scan.ticks.size()) - 1;
            int mode = scan.ticks.empty() ? 0 : scan.ticks.back().mode;
            double speedX = scan.ticksPerSecond / 240.0;
            double remaining = progress > 0.001f ? elapsed * (1.0 - progress) / progress : -1.0;
            lines[0] = fmt::format("progress {:5.1f}%   tick {}   {}{}", progress * 100.f, tick, modeName(mode),
                scan.wasDual ? " dual" : "");
            lines[1] = fmt::format("beam {}/{}   deaths {}/t   gaps {}", scan.nextCount ? scan.nextCount : scan.currentCount, scan.beamWidth,
                scan.lastDeaths, scan.gaps);
            lines[2] = fmt::format("speed {:.1f}x   {:.0f} t/s   step {:.2f}ms", speedX, scan.ticksPerSecond, scan.lastStepMs);
            lines[3] = fmt::format("cells {}   branches {}   syncs {}", countText(scan.keys.size()), countText(scan.expansions), scan.syncs);
            lines[4] = fmt::format("parity {} ({:.4f})   {} / eta {}", scan.parityBreaks, scan.parityWorst, clockText(elapsed),
                clockText(remaining));
        }
        else {
            lines[0] = scan.resultText;
            if (runtime.table) {
                lines[1] = fmt::format("map {} ticks   viable {}   dead {}", runtime.table->ticks(), countText(runtime.table->viableCount),
                    countText(runtime.table->deadCount));
            }
        }
        for (int index = 0; index < LINE_COUNT; ++index) {
            if (m_lines[index]) {
                m_lines[index]->setString(lines[index].c_str());
            }
        }
        drawFrame(progress);
    }

    void showHud(PlayLayer* layer) {
        if (analysis.hud) {
            analysis.hud->removeFromParentAndCleanup(true);
            analysis.hud->release();
            analysis.hud = nullptr;
        }
        auto* hud = DeepHud::create();
        if (!hud) {
            return;
        }
        hud->retain();
        CCNode* host = layer->getParent() ? layer->getParent() : static_cast<CCNode*>(layer);
        host->addChild(hud, 9000);
        analysis.hud = hud;
    }

    void releaseHud() {
        if (analysis.hud) {
            analysis.hud->release();
            analysis.hud = nullptr;
        }
    }

    // ------------------------------------------------------------------ beam

    Node& allocateNext() {
        if (analysis.nextCount >= analysis.next.size()) {
            analysis.next.emplace_back();
        }
        return analysis.next[analysis.nextCount++];
    }

    void trimUsed(std::vector<GameObject*>& objects, float x) {
        if (objects.size() <= MAX_USED) {
            return;
        }
        std::erase_if(objects, [x](GameObject* object) { return object->getPositionX() < x - 240.f; });
        if (objects.size() > MAX_USED) {
            objects.erase(objects.begin(), objects.begin() + static_cast<std::ptrdiff_t>(objects.size() - MAX_USED));
        }
    }

    uint64_t nodeKey(Node const& node) {
        Quant first = quantOf(node.first);
        if (node.dual) {
            Quant second = quantOf(node.second);
            return keyOf(first, &second, node.held);
        }
        return keyOf(first, nullptr, node.held);
    }

    void pressWorker(PlayerObject* worker, bool isDown) {
        if (isDown) {
            worker->pushButton(PlayerButton::Jump);
        }
        else {
            worker->releaseButton(PlayerButton::Jump);
        }
    }

    // steps one branch with one input; returns the child index or -1 when it dies
    int expand(PlayLayer* layer, Node const& node, int nodeIndex, bool input, float deltaTime) {
        auto* first = g_deepWorker1;
        auto* second = g_deepWorker2;
        bool dual = node.dual && second;
        loadState(first, node.first);
        if (dual) {
            loadState(second, node.second);
        }
        g_deepStep.used = node.used;
        g_deepStep.orbs = node.orbs;
        g_deepStep.snapped.clear();
        g_deepStep.dual = dual;
        state.hasGhostDied = false;
        state.hasGhostHitPortal = false;
        state.isHeld1 = state.isHeld2 = input;
        if (input != node.held) {
            pressWorker(first, input);
            if (dual) {
                pressWorker(second, input);
            }
        }
        ++analysis.expansions;

        first->update(deltaTime);
        if (dual) {
            second->update(deltaTime);
        }
        if (!state.hasGhostDied) {
            resetLogs(first);
            layer->checkCollisions(first, deltaTime, false);
            if (dual && !state.hasGhostDied) {
                resetLogs(second);
                layer->checkCollisions(second, deltaTime, false);
            }
        }
        int result = -1;
        if (!state.hasGhostDied) {
            first->updateRotation(deltaTime);
            if (dual) {
                second->updateRotation(deltaTime);
            }
            Quant quantFirst = quantOf(first);
            Quant quantSecond;
            if (dual) {
                quantSecond = quantOf(second);
            }
            uint64_t key = keyOf(quantFirst, dual ? &quantSecond : nullptr, input);
            auto found = analysis.childMap.find(key);
            if (found != analysis.childMap.end()) {
                result = found->second;
            }
            else {
                result = static_cast<int>(analysis.nextCount);
                Node& child = allocateNext();
                saveState(first, child.first);
                child.dual = dual;
                if (dual) {
                    saveState(second, child.second);
                }
                child.held = input;
                child.key = key;
                child.parent = nodeIndex;
                child.parentInput = input;
                child.used = g_deepStep.used;
                child.orbs = g_deepStep.orbs;
                float x = child.first.nodePos.x;
                trimUsed(child.used, x);
                trimUsed(child.orbs, x);
                analysis.childMap.emplace(key, result);
            }
        }
        else if (analysis.deathPoints.size() < 64) {
            analysis.deathPoints.push_back(first->getPosition());
        }
        restoreObjects();
        return result;
    }

    // keeps `keep` (indices into next) in that order and rewrites the previous tick's links
    void compactNext(std::vector<int> const& keep, uint32_t previousBegin, uint16_t droppedLink) {
        std::vector<int> remap(analysis.nextCount, -1);
        std::vector<Node> compacted;
        compacted.reserve(keep.size());
        for (size_t index = 0; index < keep.size(); ++index) {
            remap[keep[index]] = static_cast<int>(index);
            compacted.push_back(std::move(analysis.next[keep[index]]));
        }
        for (size_t index = 0; index < compacted.size(); ++index) {
            analysis.next[index] = std::move(compacted[index]);
        }
        analysis.nextCount = compacted.size();
        for (size_t index = 0; index < analysis.currentCount; ++index) {
            for (auto* links : { &analysis.link0, &analysis.link1 }) {
                uint16_t& link = (*links)[previousBegin + index];
                if (link < LINK_NONE) {
                    int mapped = remap[link];
                    link = mapped >= 0 ? static_cast<uint16_t>(mapped) : droppedLink;
                }
            }
        }
        if (analysis.leader >= 0 && static_cast<size_t>(analysis.leader) < remap.size()) {
            analysis.leader = remap[analysis.leader];
        }
        else {
            analysis.leader = -1;
        }
    }

    void rekeyAndMerge(uint32_t previousBegin) {
        analysis.childMap.clear();
        std::vector<int> keep;
        std::vector<int> duplicateOf(analysis.nextCount, -1);
        keep.reserve(analysis.nextCount);
        for (size_t index = 0; index < analysis.nextCount; ++index) {
            auto& node = analysis.next[index];
            node.key = nodeKey(node);
            auto found = analysis.childMap.find(node.key);
            if (found != analysis.childMap.end()) {
                duplicateOf[index] = found->second;
                continue;
            }
            analysis.childMap.emplace(node.key, static_cast<int>(index));
            keep.push_back(static_cast<int>(index));
        }
        if (keep.size() == analysis.nextCount) {
            return;
        }
        for (size_t index = 0; index < analysis.currentCount; ++index) {
            for (auto* links : { &analysis.link0, &analysis.link1 }) {
                uint16_t& link = (*links)[previousBegin + index];
                if (link < LINK_NONE && duplicateOf[link] >= 0) {
                    link = static_cast<uint16_t>(duplicateOf[link]);
                }
            }
        }
        if (analysis.leader >= 0 && duplicateOf[analysis.leader] >= 0) {
            analysis.leader = duplicateOf[analysis.leader];
        }
        compactNext(keep, previousBegin, LINK_PRUNED);
    }

    // spread the beam over modes and heights instead of keeping one dense cluster
    void prune(uint32_t previousBegin, int preferred) {
        size_t width = static_cast<size_t>(analysis.beamWidth);
        if (analysis.nextCount <= width) {
            return;
        }
        std::unordered_map<uint64_t, std::vector<int>> buckets;
        for (size_t index = 0; index < analysis.nextCount; ++index) {
            auto const& node = analysis.next[index];
            Quant quant = quantOf(node.first);
            uint64_t bucket = (static_cast<uint64_t>(quant.flags & 0x1F) << 40) ^ static_cast<uint64_t>(quant.y >> 3) ^
                (node.dual ? (static_cast<uint64_t>(quantOf(node.second).y >> 3) << 20) : 0ull);
            buckets[bucket].push_back(static_cast<int>(index));
        }
        std::vector<std::vector<int>*> lists;
        lists.reserve(buckets.size());
        for (auto& [bucket, list] : buckets) {
            std::sort(list.begin(), list.end(), [](int left, int right) {
                return analysis.next[left].first.m_yVelocity < analysis.next[right].first.m_yVelocity;
            });
            lists.push_back(&list);
        }
        std::sort(lists.begin(), lists.end(), [](auto* left, auto* right) { return left->front() < right->front(); });
        std::vector<char> chosen(analysis.nextCount, 0);
        std::vector<int> keep;
        keep.reserve(width);
        if (preferred >= 0 && static_cast<size_t>(preferred) < analysis.nextCount) {
            chosen[preferred] = 1;
            keep.push_back(preferred);
        }
        size_t round = 0;
        while (keep.size() < width) {
            bool any = false;
            for (auto* list : lists) {
                if (keep.size() >= width) {
                    break;
                }
                size_t count = list->size();
                if (round >= count) {
                    continue;
                }
                any = true;
                // visit the bucket's vy order as 0, n-1, n/2, n/4, 3n/4, ...
                size_t position;
                if (round == 0) {
                    position = 0;
                }
                else if (round == 1) {
                    position = count - 1;
                }
                else {
                    size_t step = round - 1;
                    size_t level = 1;
                    while ((1ull << level) <= step) {
                        ++level;
                    }
                    size_t denominator = 1ull << level;
                    size_t numerator = 2 * (step - (denominator >> 1)) + 1;
                    position = std::min(count - 1, numerator * count / denominator);
                }
                for (size_t probe = 0; probe < count; ++probe) {
                    int candidate = (*list)[(position + probe) % count];
                    if (!chosen[candidate]) {
                        chosen[candidate] = 1;
                        keep.push_back(candidate);
                        break;
                    }
                }
            }
            if (!any) {
                break;
            }
            ++round;
        }
        std::sort(keep.begin(), keep.end());
        compactNext(keep, previousBegin, LINK_PRUNED);
    }

    void seedNode(Node& node, PlayLayer* layer) {
        saveState(layer->m_player1, node.first);
        node.dual = layerIsDual(layer);
        if (node.dual) {
            saveState(layer->m_player2, node.second);
        }
        node.held = holdsJump(layer->m_player1);
        node.used.clear();
        node.orbs.clear();
        node.parent = -1;
        node.parentInput = false;
        node.key = nodeKey(node);
    }

    void recordTick(PlayLayer* layer, uint8_t flags) {
        TickRecord record;
        record.carrierX = layer->m_player1->getPositionX();
        record.carrierY = layer->m_player1->getPositionY();
        record.cellBegin = static_cast<uint32_t>(analysis.keys.size());
        record.cellCount = static_cast<uint32_t>(analysis.nextCount);
        record.flags = flags | (analysis.wasDual ? TICK_DUAL : 0);
        auto* player = layer->m_player1;
        record.mode = static_cast<uint8_t>(modeIndex(player->m_isShip, player->m_isBall, player->m_isBird, player->m_isDart, player->m_isRobot,
            player->m_isSpider, player->m_isSwing));
        analysis.ticks.push_back(record);
        for (size_t index = 0; index < analysis.nextCount; ++index) {
            analysis.keys.push_back(analysis.next[index].key);
            analysis.link0.push_back(LINK_NONE);
            analysis.link1.push_back(LINK_NONE);
        }
    }

    double stateDifference(PlayerObject* real, PState const& branch) {
        double difference = 0.0;
        CCPoint position = real->getPosition();
        difference = std::max<double>(difference, std::abs(position.x - branch.nodePos.x));
        difference = std::max<double>(difference, std::abs(position.y - branch.nodePos.y));
        difference = std::max(difference, std::abs(real->m_yVelocity - branch.m_yVelocity));
        if (real->m_isShip != branch.m_isShip || real->m_isBird != branch.m_isBird || real->m_isBall != branch.m_isBall ||
            real->m_isDart != branch.m_isDart || real->m_isRobot != branch.m_isRobot || real->m_isSpider != branch.m_isSpider ||
            real->m_isSwing != branch.m_isSwing || real->m_isUpsideDown != branch.m_isUpsideDown || real->m_isOnGround != branch.m_isOnGround ||
            real->m_vehicleSize != branch.m_vehicleSize || real->m_isDashing != branch.m_isDashing) {
            difference = std::max(difference, 1000.0);
        }
        return difference;
    }

    std::string differenceDetail(PlayerObject* real, PState const& branch) {
        CCPoint position = real->getPosition();
        return fmt::format("dx {:.4f} dy {:.4f} dvy {:.4f} mode {}/{} grav {}/{} ground {}/{} size {:.2f}/{:.2f}", position.x - branch.nodePos.x,
            position.y - branch.nodePos.y, real->m_yVelocity - branch.m_yVelocity,
            modeName(modeIndex(real->m_isShip, real->m_isBall, real->m_isBird, real->m_isDart, real->m_isRobot, real->m_isSpider, real->m_isSwing)),
            modeName(modeIndex(branch.m_isShip, branch.m_isBall, branch.m_isBird, branch.m_isDart, branch.m_isRobot, branch.m_isSpider, branch.m_isSwing)),
            real->m_isUpsideDown, branch.m_isUpsideDown, real->m_isOnGround, branch.m_isOnGround, real->m_vehicleSize, branch.m_vehicleSize);
    }

    void beamStep(PlayLayer* layer, float deltaTime) {
        auto stepStart = std::chrono::steady_clock::now();
        auto& scan = analysis;
        if (scan.currentCount == 0 || scan.ticks.empty() || !g_deepWorker1 || !g_deepWorker2) {
            return;
        }
        uint32_t previousBegin = scan.ticks.back().cellBegin;
        int previousLeader = scan.leader;
        bool previousLeaderHeld = previousLeader >= 0 && static_cast<size_t>(previousLeader) < scan.currentCount ? scan.current[previousLeader].held : false;
        float carrierX = layer->m_player1->getPositionX();

        swapPristine(true, carrierX);
        state.isSimulating = true;
        g_deepStep.stepping = true;
        state.simulationWaveBase = circleWaveCount(layer);
        state.hasRealRectangle = false;
        state.harmlessObjects.clear();
        state.orbClickEnabled = false;
        state.orbClickDelayTicks = 0;
        state.retapTick = -1;
        state.isProbing = false;
        state.autoReleaseTick1 = state.autoReleaseTick2 = -1;
        state.isAuto1 = state.isAuto2 = false;
        state.firstOrbRingObject = nullptr;
        state.firstOrbPlayerObject = nullptr;
        state.simulationTick = static_cast<int>(scan.ticks.size());

        scan.nextCount = 0;
        scan.childMap.clear();
        scan.deathPoints.clear();
        int deaths = 0;
        for (size_t index = 0; index < scan.currentCount; ++index) {
            Node const& node = scan.current[index];
            int keepChild = expand(layer, node, static_cast<int>(index), node.held, deltaTime);
            int toggleChild = expand(layer, node, static_cast<int>(index), !node.held, deltaTime);
            deaths += (keepChild < 0) + (toggleChild < 0);
            scan.link0[previousBegin + index] = keepChild < 0 ? LINK_DEAD : static_cast<uint16_t>(keepChild);
            scan.link1[previousBegin + index] = toggleChild < 0 ? LINK_DEAD : static_cast<uint16_t>(toggleChild);
        }
        clearSimulationWaves(layer);
        g_deepWorker1->stopAllActions();
        g_deepWorker2->stopAllActions();
        g_deepStep.stepping = false;
        g_deepStep.used.clear();
        g_deepStep.orbs.clear();
        g_deepStep.snapped.clear();
        state.isSimulating = false;
        swapPristine(false, carrierX);
        scan.deaths += static_cast<uint64_t>(deaths);
        scan.lastDeaths = deaths;

        // leader candidates before any reordering
        int leaderChild = -1;
        bool leaderToggled = false;
        if (previousLeader >= 0 && static_cast<size_t>(previousLeader) < scan.currentCount) {
            uint16_t keepLink = scan.link0[previousBegin + previousLeader];
            uint16_t toggleLink = scan.link1[previousBegin + previousLeader];
            if (keepLink < LINK_NONE) {
                leaderChild = keepLink;
            }
            else if (toggleLink < LINK_NONE) {
                leaderChild = toggleLink;
                leaderToggled = true;
            }
        }
        scan.leader = leaderChild;

        // dual entered or left by the carrier: give every branch a matching second player
        bool carrierDual = layerIsDual(layer);
        if (carrierDual != scan.wasDual && scan.nextCount > 0) {
            if (carrierDual) {
                PState realSecond;
                saveState(layer->m_player2, realSecond);
                CCPoint carrierFirst = layer->m_player1->getPosition();
                for (size_t index = 0; index < scan.nextCount; ++index) {
                    Node& node = scan.next[index];
                    CCPoint offset = node.first.nodePos - carrierFirst;
                    node.second = realSecond;
                    node.second.nodePos = node.second.nodePos + offset;
                    node.second.m_position = node.second.m_position + offset;
                    node.second.lastPosition = node.second.lastPosition + offset;
                    node.dual = true;
                }
            }
            else {
                for (size_t index = 0; index < scan.nextCount; ++index) {
                    scan.next[index].dual = false;
                }
            }
            rekeyAndMerge(previousBegin);
        }
        scan.wasDual = carrierDual;

        uint8_t flags = 0;
        if (scan.nextCount == 0) {
            // every branch died: either the level really needs something the swarm never tried, or the scan lost track.
            // nothing here is marked dead, the swarm restarts from the carrier
            for (size_t index = 0; index < scan.currentCount; ++index) {
                scan.link0[previousBegin + index] = LINK_WIPE;
                scan.link1[previousBegin + index] = LINK_WIPE;
            }
            seedNode(allocateNext(), layer);
            scan.leader = 0;
            leaderChild = 0;
            ++scan.gaps;
            flags |= TICK_GAP | TICK_RESEED;
        }
        else {
            prune(previousBegin, scan.leader);
        }

        if (scan.leader < 0 || static_cast<size_t>(scan.leader) >= scan.nextCount) {
            // the leader died: follow the branch closest to the carrier
            float carrierY = layer->m_player1->getPositionY();
            float bestDistance = 1e30f;
            int best = 0;
            for (size_t index = 0; index < scan.nextCount; ++index) {
                float distance = std::abs(scan.next[index].first.nodePos.y - carrierY);
                if (distance < bestDistance) {
                    bestDistance = distance;
                    best = static_cast<int>(index);
                }
            }
            scan.leader = best;
            leaderChild = -1;
        }

        // carrier vs leader: identical unless the input changed, the leader switched, or GD did something the branches cannot see
        Node& lead = scan.next[scan.leader];
        // only a child the leader produced itself with the carrier's input can be compared bit for bit
        bool continuing = leaderChild >= 0 && !leaderToggled && (flags & TICK_GAP) == 0 && lead.parent == previousLeader &&
            lead.parentInput == previousLeaderHeld;
        double difference = stateDifference(layer->m_player1, lead.first);
        if (lead.dual && layer->m_player2) {
            difference = std::max(difference, stateDifference(layer->m_player2, lead.second));
        }
        if (continuing && difference > 1e-3) {
            // the real game moved the carrier differently: keep the real state on the leader branch
            ++scan.parityBreaks;
            scan.parityWorst = std::max(scan.parityWorst, std::min(difference, 999.0));
            if (scan.parityLogged < 12) {
                ++scan.parityLogged;
                log(fmt::format("parity break at tick {} (x {:.1f}): {}", scan.ticks.size(), layer->m_player1->getPositionX(),
                    differenceDetail(layer->m_player1, lead.first)));
            }
            saveState(layer->m_player1, lead.first);
            if (lead.dual && layer->m_player2) {
                saveState(layer->m_player2, lead.second);
            }
            lead.held = previousLeaderHeld;
            lead.key = nodeKey(lead);
        }
        else if (!continuing && difference > 1e-6) {
            loadState(layer->m_player1, lead.first);
            if (lead.dual && layer->m_player2) {
                loadState(layer->m_player2, lead.second);
            }
            ++scan.syncs;
        }

        recordTick(layer, flags);
        if (scan.swarm) {
            drawTick(static_cast<int>(scan.ticks.size()));
        }

        std::swap(scan.current, scan.next);
        std::swap(scan.currentCount, scan.nextCount);
        scan.nextCount = 0;

        scan.lastStepMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - stepStart).count();
        float span = std::max(1.f, scan.levelLength - scan.startX);
        int decile = static_cast<int>(std::clamp((carrierX - scan.startX) / span, 0.f, 0.999f) * 10.f);
        if (decile != scan.lastProgressDecile) {
            scan.lastProgressDecile = decile;
            log(fmt::format("{}0% | tick {} | beam {} | cells {} | gaps {} | parity {} | syncs {}", decile, scan.ticks.size() - 1, scan.currentCount,
                scan.keys.size(), scan.gaps, scan.parityBreaks, scan.syncs));
        }
    }

    // ------------------------------------------------------------------ finish

    struct FinishJob {
        int levelID = 0;
        uint64_t levelHash = 0;
        bool reachedEnd = false;
        std::vector<TickRecord> ticks;
        std::vector<uint64_t> keys;
        std::vector<uint16_t> link0;
        std::vector<uint16_t> link1;
    };

    std::unique_ptr<Table> buildTable(FinishJob& job) {
        auto table = std::make_unique<Table>();
        table->levelID = job.levelID;
        table->levelHash = job.levelHash;
        table->complete = job.reachedEnd;
        int tickCount = static_cast<int>(job.ticks.size());
        std::vector<uint8_t> fates(job.keys.size(), FATE_UNKNOWN);
        if (tickCount == 0) {
            return table;
        }
        int lastTick = tickCount - 1;
        {
            auto const& record = job.ticks[lastTick];
            for (uint32_t cell = 0; cell < record.cellCount; ++cell) {
                fates[record.cellBegin + cell] = job.reachedEnd ? FATE_VIABLE : FATE_FRONTIER;
            }
        }
        auto childFate = [&](uint16_t link, int nextTick) -> uint8_t {
            if (link == LINK_DEAD) {
                return FATE_DEAD;
            }
            if (link >= LINK_NONE) {
                return FATE_UNKNOWN;
            }
            auto const& record = job.ticks[nextTick];
            if (link >= record.cellCount) {
                return FATE_UNKNOWN;
            }
            return fates[record.cellBegin + link];
        };
        for (int tick = lastTick - 1; tick >= 0; --tick) {
            auto const& record = job.ticks[tick];
            for (uint32_t cell = 0; cell < record.cellCount; ++cell) {
                uint32_t index = record.cellBegin + cell;
                uint8_t first = childFate(job.link0[index], tick + 1);
                uint8_t second = childFate(job.link1[index], tick + 1);
                uint8_t fate;
                if (first == FATE_VIABLE || second == FATE_VIABLE) {
                    fate = FATE_VIABLE;
                }
                else if (first == FATE_FRONTIER || second == FATE_FRONTIER) {
                    fate = lastTick - tick >= FRONTIER_TICKS ? FATE_VIABLE : FATE_FRONTIER;
                }
                else if (first == FATE_UNKNOWN || second == FATE_UNKNOWN) {
                    fate = FATE_UNKNOWN;
                }
                else {
                    fate = FATE_DEAD;
                }
                fates[index] = fate;
            }
        }
        table->xs.reserve(tickCount);
        table->begin.reserve(tickCount + 1);
        std::vector<std::pair<uint64_t, uint8_t>> cells;
        for (int tick = 0; tick < tickCount; ++tick) {
            auto const& record = job.ticks[tick];
            table->xs.push_back(record.carrierX);
            table->begin.push_back(static_cast<uint32_t>(table->keys.size()));
            cells.clear();
            for (uint32_t cell = 0; cell < record.cellCount; ++cell) {
                uint8_t fate = fates[record.cellBegin + cell];
                if (fate == FATE_VIABLE || fate == FATE_DEAD) {
                    cells.emplace_back(job.keys[record.cellBegin + cell], fate);
                }
            }
            std::sort(cells.begin(), cells.end());
            cells.erase(std::unique(cells.begin(), cells.end(), [](auto const& left, auto const& right) { return left.first == right.first; }),
                cells.end());
            for (auto const& [key, fate] : cells) {
                table->keys.push_back(key);
                table->labels.push_back(fate);
                (fate == FATE_VIABLE ? table->viableCount : table->deadCount)++;
            }
        }
        table->begin.push_back(static_cast<uint32_t>(table->keys.size()));
        return table;
    }

    void stopAudio() {
        if (auto* engine = FMODAudioEngine::sharedEngine()) {
            engine->stopAllMusic(true);
            engine->stopAllEffects();
        }
    }

    void finish(PlayLayer* layer, bool resetAfter) {
        auto& scan = analysis;
        if (!scan.active) {
            return;
        }
        bool keep = !scan.abort && scan.ticks.size() > 1;
        auto job = std::make_shared<FinishJob>();
        job->levelID = scan.levelID;
        job->levelHash = scan.levelHash;
        job->reachedEnd = scan.reachedEnd;
        job->ticks = std::move(scan.ticks);
        job->keys = std::move(scan.keys);
        job->link0 = std::move(scan.link0);
        job->link1 = std::move(scan.link1);
        double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - scan.startTime).count();
        log(fmt::format("scan {} after {} ticks in {} | gaps {} | parity breaks {} (worst {:.4f}) | syncs {} | {} branches", scan.abort ? "aborted" :
            scan.reachedEnd ? "reached the end" : "stopped", job->ticks.size(), clockText(elapsed), scan.gaps, scan.parityBreaks, scan.parityWorst,
            scan.syncs, scan.expansions));

        scan.active = false;
        g_deepActive = false;
        scan.paused = false;
        scan.pendingFinish = false;
        scan.current.clear();
        scan.next.clear();
        scan.currentCount = scan.nextCount = 0;
        scan.childMap.clear();
        scan.pristine.clear();
        scan.pristineSeen.clear();
        clearViz();
        // the branch players stay until the level closes: GD keeps raw pointers to players (trails, particles) that only go
        // away with the level, the same lifetime afterflow's own ghosts have

        if (keep) {
            scan.finishing = true;
            scan.resultText = "building map...";
            int levelID = job->levelID;
            uint64_t levelHash = job->levelHash;
            auto path = dataPath(levelID, levelHash);
            std::thread([job, levelID, levelHash, path] {
                std::shared_ptr<Table> table(buildTable(*job).release());
                bool written = saveTable(*table, path);
                geode::queueInMainThread([table, written, levelID, levelHash] {
                    analysis.finishing = false;
                    analysis.resultText = fmt::format("{} {} ticks   viable {}   dead {}", written ? "saved" : "save failed", table->ticks(),
                        countText(table->viableCount), countText(table->deadCount));
                    log(analysis.resultText);
                    if (runtime.levelID == levelID && runtime.levelHash == levelHash) {
                        runtime.table = table;
                        runtime.synced = false;
                    }
                    if (analysis.hud) {
                        analysis.hud->refresh();
                        analysis.hud->finishLater(8.f);
                        releaseHud();
                    }
                });
            }).detach();
        }
        else {
            scan.resultText = scan.abort ? "aborted" : "too short, nothing saved";
            if (scan.hud) {
                scan.hud->refresh();
                scan.hud->finishLater(4.f);
                releaseHud();
            }
        }

        if (layer && resetAfter) {
            scan.suppressAttempts = true;
            layer->resetLevel();
            scan.suppressAttempts = false;
        }
    }

    // ------------------------------------------------------------------ control

    bool canScan(PlayLayer* layer, std::string& reason) {
        if (!layer || !layer->m_player1) {
            reason = "open a level first";
            return false;
        }
        if (layer->m_isPlatformer) {
            reason = "platformer levels are not supported";
            return false;
        }
        if (layer->m_level && layer->m_level->m_twoPlayerMode) {
            reason = "2-player levels are not supported";
            return false;
        }
        if (layer->m_isPracticeMode) {
            reason = "leave practice mode first";
            return false;
        }
        if (analysis.finishing) {
            reason = "the previous map is still being built";
            return false;
        }
        return true;
    }

    void start() {
        auto* layer = PlayLayer::get();
        std::string reason;
        if (!canScan(layer, reason)) {
            log(reason);
            return;
        }
        readSettings();
        auto& scan = analysis;
        scan.ticks.clear();
        scan.keys.clear();
        scan.link0.clear();
        scan.link1.clear();
        scan.current.clear();
        scan.next.clear();
        scan.currentCount = scan.nextCount = 0;
        scan.pristine.clear();
        scan.pristineSeen.clear();
        scan.expansions = scan.deaths = 0;
        scan.gaps = scan.parityBreaks = scan.syncs = 0;
        scan.parityWorst = 0.0;
        scan.parityLogged = 0;
        scan.lastProgressDecile = -1;
        scan.reachedEnd = false;
        scan.abort = false;
        scan.paused = false;
        scan.pendingFinish = false;
        scan.speedMode = 2;
        scan.stepsPerFrame = 8;
        scan.layer = layer;
        scan.levelID = levelIdOf(layer);
        scan.levelHash = levelHashOf(layer);
        scan.levelLength = layer->m_levelLength;
        scan.resultText.clear();

        if (!g_deepWorker1 || !g_deepWorker2) {
            destroyWorkers();
            g_deepWorker1 = makeWorker(layer, false);
            g_deepWorker2 = makeWorker(layer, true);
        }
        if (!g_deepWorker1 || !g_deepWorker2) {
            destroyWorkers();
            log("could not create the branch players");
            return;
        }

        resetPending();
        state.hasAppliedInput = false;
        state.hasRealInput = false;
        layer->m_player1->releaseButton(PlayerButton::Jump);
        if (layer->m_player2) {
            layer->m_player2->releaseButton(PlayerButton::Jump);
        }
        scan.suppressAttempts = true;
        layer->resetLevel();
        scan.suppressAttempts = false;

        scan.active = true;
        g_deepActive = true;
        scan.wasDual = layerIsDual(layer);
        scan.startX = layer->m_player1->getPositionX();
        scan.nextCount = 0;
        seedNode(allocateNext(), layer);
        scan.leader = 0;
        recordTick(layer, 0);
        std::swap(scan.current, scan.next);
        std::swap(scan.currentCount, scan.nextCount);
        scan.nextCount = 0;
        scan.carrierUpdates = scan.lastStepped = 0;
        scan.startTime = scan.rateTime = std::chrono::steady_clock::now();
        scan.rateTicks = 0;
        scan.ticksPerSecond = 0.0;

        stopAudio();
        createViz(layer);
        showHud(layer);
        log(fmt::format("scan started | level {} | beam {} | budget {:.0f}ms", scan.levelID, scan.beamWidth, scan.budgetMs));
        auto missing = missingFields<PlayerObject>();
        if (!missing.empty()) {
            std::string names;
            for (auto const* name : missing) {
                names += names.empty() ? "" : " ";
                names += name;
            }
            log(fmt::format("{} player fields are not in these bindings: {}", missing.size(), names));
        }
    }

    void requestStop(bool abortScan) {
        if (!analysis.active) {
            return;
        }
        analysis.abort = abortScan;
        analysis.pendingFinish = true;
    }

    void toggle() {
        if (analysis.active) {
            requestStop(false);
        }
        else {
            start();
        }
    }

    void togglePause() {
        if (analysis.active) {
            analysis.paused = !analysis.paused;
        }
    }

    void setSpeed(int mode) {
        analysis.speedMode = std::clamp(mode, 0, 2);
    }

    bool active() {
        return analysis.active;
    }

    // runs the frame for the base layer; returns false when the caller should run the original update itself
    template <class Original>
    bool driveUpdate(GJBaseGameLayer* baseLayer, float deltaTime, Original&& original) {
        auto& scan = analysis;
        auto* layer = PlayLayer::get();
        if (!scan.active || !layer || baseLayer != layer) {
            return false;
        }
        if (scan.pendingFinish) {
            finish(layer, true);
            original(deltaTime);
            return true;
        }
        if (++scan.frameCounter % 30 == 0) {
            stopAudio();
        }
        if (scan.paused) {
            original(0.f);
            return true;
        }
        float stepTime = std::min(1.f, layer->m_gameState.m_timeWarp) / 240.f;
        if (!(stepTime > 0.f)) {
            stepTime = 1.f / 240.f;
        }
        float frameDelta;
        if (scan.speedMode == 0) {
            frameDelta = deltaTime;
        }
        else if (scan.speedMode == 1) {
            frameDelta = deltaTime * 4.f;
        }
        else {
            frameDelta = scan.stepsPerFrame * stepTime;
        }
        uint64_t before = scan.carrierUpdates;
        auto frameStart = std::chrono::steady_clock::now();
        original(frameDelta);
        double frameMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frameStart).count();
        uint64_t stepped = scan.carrierUpdates - before;
        if (stepped > 0) {
            double perStep = frameMs / static_cast<double>(stepped);
            int wanted = static_cast<int>(scan.budgetMs / std::max(perStep, 0.01));
            scan.stepsPerFrame = std::clamp((scan.stepsPerFrame * 3 + std::clamp(wanted, 1, 4000)) / 4, 1, 4000);
            scan.rateTicks += stepped;
        }
        auto now = std::chrono::steady_clock::now();
        double rateSpan = std::chrono::duration<double>(now - scan.rateTime).count();
        if (rateSpan >= 0.5) {
            scan.ticksPerSecond = scan.rateTicks / rateSpan;
            scan.rateTicks = 0;
            scan.rateTime = now;
        }
        return true;
    }

    // called after GD's own checkCollisions for the real first player
    void afterCarrierCollisions(PlayLayer* layer, float deltaTime) {
        auto& scan = analysis;
        if (!scan.active || scan.pendingFinish || scan.carrierUpdates == scan.lastStepped) {
            return;
        }
        scan.lastStepped = scan.carrierUpdates;
        beamStep(layer, deltaTime);
    }

    // start of the real first player's update
    void onRealUpdateBegin(PlayerObject* player) {
        if (analysis.active) {
            ++analysis.carrierUpdates;
            return;
        }
        auto const* table = runtime.table.get();
        if (!table) {
            return;
        }
        float x = player->getPositionX();
        int tick = runtime.rtTick;
        if (!runtime.synced || !table->xMatches(tick, x, X_MATCH)) {
            tick = table->findTick(x, runtime.synced ? tick : -1);
            runtime.synced = tick >= 0;
        }
        runtime.rtTick = runtime.synced ? tick + 1 : 0;
    }

    bool blockRealDeath(PlayLayer* layer, PlayerObject* player, GameObject* object) {
        if (!analysis.active || state.isSimulating || !layer) {
            return false;
        }
        if (player != layer->m_player1 && player != layer->m_player2) {
            return false;
        }
        // the anticheat spike only verifies damage, let GD see it
        return !(object && layer->m_anticheatSpike && object == layer->m_anticheatSpike);
    }

    bool interceptEnd() {
        if (!analysis.active) {
            return false;
        }
        analysis.reachedEnd = true;
        analysis.pendingFinish = true;
        return true;
    }

    void onLevelQuit(PlayLayer* layer) {
        if (analysis.active) {
            finish(layer, false);
        }
        if (analysis.hud) {
            analysis.hud->release();
            analysis.hud = nullptr;
        }
        clearViz();
        destroyWorkers();
    }

    bool suppressAttempts() {
        return analysis.suppressAttempts;
    }

    void setupKeybinds() {
        listenForKeybindSettingPresses(KEYBIND_DEEP, [](Keybind const&, bool isDown, bool isRepeat, double) {
            if (isDown && !isRepeat) {
                toggle();
            }
            return true;
        });
        listenForKeybindSettingPresses(KEYBIND_DEEP_PAUSE, [](Keybind const&, bool isDown, bool isRepeat, double) {
            if (isDown && !isRepeat) {
                togglePause();
            }
            return true;
        });
    }

#else

    bool active() { return false; }
    bool isWorker(PlayerObject*) { return false; }
    void ghostFlipGravity(GJBaseGameLayer*, PlayerObject* player, bool flip) { player->flipGravity(flip, true); }
    int runtimeBase() { return -1; }
    int label(int, PlayerObject*, PlayerObject*, bool, bool) { return 0; }
    void onLevelInit(PlayLayer*) {}
    void onResetLevel() {}
    void onLevelQuit(PlayLayer*) {}
    void recordCarrierCheck(EffectGameObject*) {}
    void toggle() {}
    void togglePause() {}
    void afterCarrierCollisions(PlayLayer*, float) {}
    void onRealUpdateBegin(PlayerObject*) {}
    bool blockRealDeath(PlayLayer*, PlayerObject*, GameObject*) { return false; }
    bool interceptEnd() { return false; }
    bool suppressAttempts() { return false; }
    void setupKeybinds() {}
    template <class Original>
    bool driveUpdate(GJBaseGameLayer*, float, Original&&) { return false; }
    struct RuntimeStub { int alignMargin = 0; } runtime;

#endif

}
