package io.netty.channel;

import io.netty.util.concurrent.EventExecutor;

public interface ChannelHandlerContext {
    EventExecutor executor();
    ChannelHandlerContext fireChannelRead(Object msg);
    ChannelFuture write(Object msg, ChannelPromise promise);
    ChannelHandlerContext flush();
}
