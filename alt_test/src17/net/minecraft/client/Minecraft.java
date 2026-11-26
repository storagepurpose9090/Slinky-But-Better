package net.minecraft.client;

import net.minecraft.util.Session;

public class Minecraft {
    public static final Minecraft theMinecraft = new Minecraft();
    private Session session = new Session("LauncherName", "premium-token-1234567890abcdef");

    public Session getSession() { return session; }
}
