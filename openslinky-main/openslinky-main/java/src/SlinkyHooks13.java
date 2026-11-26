public class SlinkyHooks13 {
    public static native void getMouseOver(Object entityRenderer, float partialTicks);

    public static native boolean isDisplayActivePreRender();

    public static native void updateDisplay();

    public static native void postWorldClear_1_7(int mask);

    public static native void postWorldClear_1_8(int mask);

    public static native void clickMouse(Object minecraft);

    public static native void rightClickMouse(Object minecraft);

    public static native void orientCamera(Object entityRenderer, float partialTicks);

    public static native void updateActiveRenderInfo(Object player, boolean thirdPersonView);

    public static native void setRenderX(Object entityRenderer, float angleX);

    public static native void setRenderY(Object entityRenderer, float angleY);

    public static native void updateEntities(Object world);

    public static native boolean canRenderName(Object renderer, Object entity);

    public static native void renderPlayer(
        Object renderPlayer,
        Object clientPlayer,
        double x,
        double y,
        double z,
        float entityYaw,
        float partialTicks
    );

    public static native void unloadChunk(Object chunkProvider, int chunkX, int chunkZ);

    public static native boolean setBlock_1_7(
        Object world,
        int x,
        int y,
        int z,
        Object block,
        int metadata,
        int flags
    );

    public static native boolean setBlock_1_8(
        Object world,
        Object blockPos,
        Object blockState,
        int flags
    );

    public static native void invalidateChunk(
        Object world,
        int minX,
        int minY,
        int minZ,
        int maxX,
        int maxY,
        int maxZ
    );

    public static native boolean shouldClipLedge(Object entity);

    public static native void moveFlying(Object entity, float strafe, float forward, float friction);

    public static native void sendPacket_1_7(Object netHandler, Object packet, Object listeners);

    public static native void sendPacket_1_8(Object netHandler, Object packet);

    public static native void updateController(Object playerController);
}
