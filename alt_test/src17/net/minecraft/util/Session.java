package net.minecraft.util;

public class Session {
    private final String username;
    private final String sessionID;

    public Session(String username, String sessionID) {
        this.username = username;
        this.sessionID = sessionID;
    }

    public String getUsername() { return username; }
    public String getSessionID() { return sessionID; }
}
