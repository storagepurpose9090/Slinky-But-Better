package net.minecraft.util;

public class Session {
    private final String username;
    private final String playerID;
    private final String token;
    private final String sessionType;

    public Session(String username, String playerID, String token, String sessionType) {
        this.username = username;
        this.playerID = playerID;
        this.token = token;
        this.sessionType = sessionType;
    }

    public String getUsername() { return username; }
    public String getPlayerID() { return playerID; }
    public String getToken() { return token; }
    public String getSessionType() { return sessionType; }
}
