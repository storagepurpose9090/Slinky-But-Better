package net.minecraft.client;

import net.minecraft.util.Session;

public class Minecraft {
    public static final Minecraft theMinecraft = new Minecraft();
    private Session session = new Session(
        "LauncherName",
        "00000000-0000-0000-0000-000000000000",
        "premium-token-1234567890abcdef",
        "mojang");

    public Session getSession() { return session; }
}
