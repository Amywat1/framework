/**
 * @file    snack_player.h
 * @brief   snack SDK 音乐播放接口
 */

#ifndef SNACK_PLAYER_H
#define SNACK_PLAYER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 播放指定编号的音频文件
 * @param item 音频编号（对应 /home/neardi/Music/00N.mp3）
 */
extern void player_play(uint8_t item);

#ifdef __cplusplus
}
#endif

#endif /* SNACK_PLAYER_H */
