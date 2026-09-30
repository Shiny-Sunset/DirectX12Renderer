#pragma once

// ゲームバランスの調整値をまとめたファイル
// 数値をいじるときは、まずここを見る
namespace GameConfig
{
    // ------------------------------------------------------------
    // プレイヤー
    // ------------------------------------------------------------
    namespace Player
    {
        constexpr int MaxHP = 3;
        constexpr float WalkSpeed = 1.8f;        // m/秒
        constexpr float RunSpeed = 4.0f;
        constexpr float JumpHeight = 1.2f;       // m
        constexpr float TurnSpeed = 12.0f;  // 向きを変える速さ
        constexpr float AttackDuration = 0.5f;   // 攻撃全体の長さ（秒）
        constexpr float HitStart = 0.15f;        // 判定が出る時刻
        constexpr float HitEnd = 0.30f;          // 判定が消える時刻
        constexpr float DamagedTime = 0.4f; // のけぞっている時間
        constexpr float InvincibleTime = 1.0f;   // 被弾後の無敵時間
    }

    // ------------------------------------------------------------
    // アリ
    // ------------------------------------------------------------
    namespace Enemy
    {
        constexpr int MaxHP = 2;

        // 敵が狙う対象
        struct Target
        {
            DirectX::XMFLOAT3 position;
            float radius;
        };

        // -- 定数：探索と移動 --
        constexpr float DetectRange = 5.0f;
        constexpr float StopDistance = 0.4f;     // 相手の表面からこの距離で止まる
        constexpr float LoseRange = 8.0f;
        constexpr float ChaseSpeed = 1.0f;
        constexpr float TurnSpeed = 8.0f;
        constexpr float AggroTime = 5.0f;   // 殴られてからプレイヤーを追う時間（秒）

        // -- 定数：戦闘 --
        constexpr float AttackRange = 0.6f;
        constexpr float AttackDuration = 1.0f;
        constexpr float AttackHitTime = 0.4f;
        constexpr float AttackCooldown = 1.5f;
        constexpr float DamagedTime = 0.4f;
        constexpr float KnockbackSpeed = 4.0f;
        constexpr float Radius = 0.3f;
    }

    // ------------------------------------------------------------
    // 王
    // ------------------------------------------------------------
    namespace King
    {
        constexpr int MaxHP = 50;
        constexpr float Scale = 1.0f;
        constexpr float BaseRadius = 0.7f;
    }

    // ------------------------------------------------------------
    // 巣穴とスポーン
    // ------------------------------------------------------------
    namespace Spawn
    {
        constexpr float StartInterval = 8.0f;    // 開始時の間隔（秒）
        constexpr float MinInterval = 1.6f;      // 最短の間隔
        constexpr float RampTime = 120.0f;        // この秒数かけて最短まで詰める
        constexpr float NestDistance = 12.0f;    // 巣穴の位置（中心からの距離）
        constexpr size_t PoolSize = 30;          // 同時に存在できるアリの数
    }

    // ------------------------------------------------------------
    // スコア
    // ------------------------------------------------------------
    namespace Score
    {
        constexpr int PointPerSecond = 10;
        constexpr int PointPerKill = 50;
    }

    // 実行中に ImGui から変える調整値
    // 定数を何倍にするか、という形で持つ
    struct Tuning
    {
        float spawnIntervalScale = 1.0f;   // 大きいほどアリが来ない
        float antSpeedScale = 1.0f;        // 大きいほどアリが速い
        int kingMaxHP = King::MaxHP;       // 次のリスタートから反映
    };
}