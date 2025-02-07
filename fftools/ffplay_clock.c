#include <stdio.h>
#include <math.h>

#include "ffplay_clock.h"

#define AV_NOSYNC_THRESHOLD 10.0

/*
从功能角度来说，get_clock 函数主要用在以下场景：

需要获取当前媒体播放位置时
比如你想知道视频播放到第几秒了
或者需要在界面上显示当前播放进度
音视频同步时
在播放器中，音频和视频是分开解码和播放的
通过这个函数可以知道音频或视频当前应该播放到什么时间点
从而判断是否需要加快或延迟某一帧的显示，保证音视频同步
特殊播放状态处理
暂停时：返回暂停时的时间点
变速播放时：根据播放速度计算正确的时间点
拖动进度条（seek）后：重新计算新的播放时间点
简单来说，这个函数就像播放器的"时钟"，告诉播放器现在播放到哪个时间点了，是音视频同步和播放控制的核心功能之一。



》 *c->queue_serial != c->serial 这个判断是干啥的呢？

这个判断是用来处理播放队列变化的情况，让我解释一下具体场景：

序列号（serial）的作用：
在播放器中，音视频数据是放在队列中的
每个队列都有一个序列号（queue_serial）
每个时钟也有一个序列号（c->serial）
什么时候序列号会改变：
最典型的场景是用户进行 seek（拖动进度条）操作
当用户拖动进度条时，旧的数据队列就不再有效
这时候会清空队列，并增加序列号
新的数据会放入新的队列中，使用新的序列号
为什么要做这个判断：
如果序列号不匹配，说明当前时钟对应的数据队列已经失效了
这时候返回 NAN 表示当前时钟值无效
这样可以防止使用旧队列的时间戳，避免播放错乱
简单举个例子：

假设你正在播放视频的第10秒
这时候你把进度条拖到第50秒
这个操作会导致序列号增加
原来队列里第10秒附近的数据就都无效了
通过序列号检查，就能知道需要等待新的数据（第50秒附近的）而不是继续使用旧数据
这是一个很巧妙的设计，用序列号来确保播放器在seek等操作后能够正确同步，避免使用无效的时间戳。

*/
double get_clock(Clock *c)
{
    if (*c->queue_serial != c->serial)
        return NAN;
    if (c->paused) {
        return c->pts;
    } else {
        double time = av_gettime_relative() / 1000000.0;
        return c->pts_drift + time - (time - c->last_updated) * (1.0 - c->speed);
    }
}

void set_clock_at(Clock *c, double pts, int serial, double time)
{
    c->pts = pts;
    c->last_updated = time;
    c->pts_drift = c->pts - time;
    c->serial = serial;
}

void set_clock(Clock *c, double pts, int serial)
{
    double time = av_gettime_relative() / 1000000.0;
    set_clock_at(c, pts, serial, time);
}

/*
set_clock_speed 是用来设置播放速度的函数，让我解释它的功能：

1. 使用场景：
当用户想要调整播放速度时使用，比如：
想要2倍速播放视频
想要0.5倍速慢放视频
或者恢复到1.0倍正常速度

2. 为什么要这样实现：
在改变速度前，先调用get_clock获取当前准确的播放时间点
然后用set_clock重新设置时钟，确保时间连续性
最后才改变速度值
这样可以避免切换速度时出现时间跳跃或不连续的情况
*/
void set_clock_speed(Clock *c, double speed)
{
    // 先通过get_clock获取当前时间点，并用set_clock重新设置时钟
    // 这样可以保证切换速度时的时间点是连续的
    set_clock(c, get_clock(c), c->serial);
    // 然后更新速度值
    c->speed = speed;
}

void init_clock(Clock *c, int *queue_serial)
{
    c->speed = 1.0;
    c->paused = 0;
    c->queue_serial = queue_serial;
    set_clock(c, NAN, -1);
}


/*
sync_clock_to_slave 是一个时钟同步函数，用于将一个时钟（主时钟）同步到另一个时钟（从时钟）。这在音视频同步中非常重要。让我详细解释：

使用场景：
通常在音视频同步时使用
比如：把视频时钟同步到音频时钟
因为一般音频时钟更稳定，所以通常作为主时钟

同步触发条件：
从时钟必须有有效时间（不是NAN）
主时钟时间无效（是NAN）
或者两个时钟的时间差超过阈值（AV_NOSYNC_THRESHOLD，定义为10.0秒）
为什么需要这个函数：
音视频是分开解码的，会产生时间差
如果时间差太大，就会出现音画不同步
这个函数通过定期同步，确保音视频保持同步
通过设置阈值，避免频繁小幅度调整造成的抖动

>  为啥这个阈值设置的10s这么大？？单位是 秒吗？
是的，AV_NOSYNC_THRESHOLD 的单位是秒，设置为10秒确实看起来很大。让我解释下原因：

这个阈值的作用：
它是一个容忍度的设置
只有当音视频时间差超过这个值时，才强制同步
小于这个值的差异会通过其他更平滑的方式来调整
为什么设这么大：
这是一个最大容忍值，是最后的保护机制
在正常播放时，音视频不会差这么多
实际上FFplay有其他更精细的同步机制，会在差异小得多时就开始调整
这个10秒的阈值主要是用来处理一些特殊情况：
网络流媒体可能会有长时间的缓冲
系统负载高时解码可能会暂时卡顿
某些格式的媒体文件在seek后需要较长时间才能重新同步
如果设得太小会有什么问题：
可能会导致频繁的强制同步在网络不好或系统负载高的情况下可能会造成画面跳跃影响观看体验

所以这个10秒更像是一个安全阈值，是最后的保护机制，而不是日常同步用的阈值。在正常播放时，音视频会通过其他更精细的机制保持同步，不会等到差异达到10秒才处理。
*/
void sync_clock_to_slave(Clock *c, Clock *slave)
{
    // 获取两个时钟的当前时间
    double clock = get_clock(c);         // 主时钟当前时间
    double slave_clock = get_clock(slave); // 从时钟当前时间

    // 判断是否需要同步的条件：
    if (!isnan(slave_clock) &&                                    // 从时钟时间必须有效
        (isnan(clock) || fabs(clock - slave_clock) > AV_NOSYNC_THRESHOLD))  // 主时钟无效或差距过大
    {
        // 将主时钟同步到从时钟
        set_clock(c, slave_clock, slave->serial);
    }
}
