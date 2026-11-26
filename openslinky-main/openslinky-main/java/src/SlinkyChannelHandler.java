import io.netty.channel.ChannelDuplexHandler;
import io.netty.channel.ChannelHandlerContext;
import io.netty.channel.ChannelPromise;
import java.util.ArrayDeque;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;

public class SlinkyChannelHandler extends ChannelDuplexHandler implements Runnable {
    private static final AtomicInteger counter = new AtomicInteger();
    private final ArrayDeque<Object[]> in = new ArrayDeque<>();
    private final ArrayDeque<Object[]> out = new ArrayDeque<>();
    private Future<?> pollTask;

    public SlinkyChannelHandler() {
    }

    @Override
    public void write(ChannelHandlerContext ctx, Object msg, ChannelPromise promise) throws Exception {
        int packetId = counter.getAndIncrement();
        int action = this.onPacket(msg, true, 0L, packetId);
        if (action == 2) {
            return;
        }
        if ((action == 0 && !this.out.isEmpty()) || action == 1) {
            this.out.add(new Object[]{ctx, msg, promise, System.currentTimeMillis(), packetId});
            return;
        }
        super.write(ctx, msg, promise);
    }

    @Override
    public void channelRead(ChannelHandlerContext ctx, Object msg) throws Exception {
        int packetId = counter.getAndIncrement();
        int action = this.onPacket(msg, false, 0L, packetId);
        if (action == 2) {
            return;
        }
        if ((action == 0 && !this.in.isEmpty()) || action == 1) {
            this.in.add(new Object[]{ctx, msg, System.currentTimeMillis(), packetId});
            return;
        }
        super.channelRead(ctx, msg);
    }

    @Override
    public void handlerAdded(ChannelHandlerContext ctx) throws Exception {
        this.pollTask = ctx.executor().scheduleWithFixedDelay(this, 4L, 4L, TimeUnit.MILLISECONDS);
    }

    @Override
    public void handlerRemoved(ChannelHandlerContext ctx) throws Exception {
        this.pollTask.cancel(false);
        while (!this.in.isEmpty()) {
            Object[] queued = this.in.poll();
            Object packet = queued[1];
            ctx.fireChannelRead(packet);
        }
        while (!this.out.isEmpty()) {
            Object[] queued = this.out.poll();
            Object packet = queued[1];
            ChannelPromise promise = (ChannelPromise) queued[2];
            ctx.write(packet, promise);
        }
    }

    @Override
    public void run() {
        long now = System.currentTimeMillis();
        this.pollInbound(now);
        this.pollOutbound(now);
    }

    private void pollInbound(long now) {
        while (!this.in.isEmpty()) {
            Object[] queued = this.in.peek();
            Object packet = queued[1];
            long elapsed = now - (Long) queued[2];
            int packetId = (Integer) queued[3];
            switch (this.onPacket(packet, false, elapsed, packetId)) {
                case 1:
                    return;
                case 2:
                    this.in.removeFirst();
                    return;
                default:
                    ChannelHandlerContext ctx = (ChannelHandlerContext) queued[0];
                    ctx.fireChannelRead(packet);
                    this.in.removeFirst();
                    break;
            }
        }
    }

    private void pollOutbound(long now) {
        ChannelHandlerContext flushCtx = null;
        loop:
        while (!this.out.isEmpty()) {
            Object[] queued = this.out.peek();
            Object packet = queued[1];
            long elapsed = now - (Long) queued[3];
            int packetId = (Integer) queued[4];
            switch (this.onPacket(packet, true, elapsed, packetId)) {
                case 1:
                    break loop;
                case 2:
                    this.out.removeFirst();
                    return;
                default:
                    ChannelHandlerContext ctx = (ChannelHandlerContext) queued[0];
                    ChannelPromise promise = (ChannelPromise) queued[2];
                    ctx.write(packet, promise);
                    this.out.removeFirst();
                    flushCtx = ctx;
                    break;
            }
        }
        if (flushCtx != null) {
            flushCtx.flush();
        }
    }

    private native int onPacket(Object packet, boolean outbound, long elapsedMs, int packetId);
}
