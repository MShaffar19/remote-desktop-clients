//
//  VncBridge.c
//  bVNC
//
//  Created by iordan iordanov on 2019-12-26.
//  Copyright © 2019 iordan iordanov. All rights reserved.
//

#include <pthread/pthread.h>
#include "VncBridge.h"
#include "ucs2xkeysym.h"

char* HOST_AND_PORT = NULL;
char* USERNAME = NULL;
char* PASSWORD = NULL;
char* CA_PATH = NULL;
rfbClient *cl = NULL;
uint8_t* pixelBuffer = NULL;
int pixel_buffer_size = 0;
bool maintainConnection = true;
int BYTES_PER_PIXEL = 4;
int fbW = 0;
int fbH = 0;

bool getMaintainConnection() {
    return maintainConnection;
}

static rfbCredential* get_credential(rfbClient* cl, int credentialType){
    rfbClientLog("VeNCrypt authentication callback called\n\n");
    rfbCredential *c = malloc(sizeof(rfbCredential));
    
    if(credentialType == rfbCredentialTypeUser) {
        rfbClientLog("Username and password requested for authentication, initializing now\n");
        c->userCredential.username = malloc(RFB_BUF_SIZE);
        c->userCredential.password = malloc(RFB_BUF_SIZE);
        strcpy(c->userCredential.username, USERNAME);
        strcpy(c->userCredential.password, PASSWORD);
        /* remove trailing newlines */
        c->userCredential.username[strcspn(c->userCredential.username, "\n")] = 0;
        c->userCredential.password[strcspn(c->userCredential.password, "\n")] = 0;
    } else if (credentialType == rfbCredentialTypeX509) {
        rfbClientLog("x509 certificates requested for authentication, initializing now\n");
        c->x509Credential.x509CACertFile = malloc(strlen(CA_PATH));
        strcpy(c->x509Credential.x509CACertFile, CA_PATH);
        c->x509Credential.x509CrlVerifyMode = rfbX509CrlVerifyNone;
        c->x509Credential.x509CACrlFile = false;
        c->x509Credential.x509ClientKeyFile = false;
        c->x509Credential.x509ClientCertFile = false;
    }
    
    return c;
}

static char* get_password(rfbClient* cl){
    rfbClientLog("VNC password authentication callback called\n\n");
    char *p = malloc(RFB_BUF_SIZE);
    
    rfbClientLog("Password requested for authentication\n");
    strcpy(p, PASSWORD);
    
    /* remove trailing newlines */
    return p;
}

static void update (rfbClient *cl, int x, int y, int w, int h) {
    //rfbClientLog("Update received\n");
    framebuffer_update_callback(cl->frameBuffer, fbW, fbH, x, y, w, h);
}

static rfbBool resize (rfbClient *cl) {
    rfbClientLog("Resize RFB Buffer, allocating buffer\n");
    static char first = TRUE;
    fbW = cl->width;
    fbH = cl->height;
    rfbClientLog("Width, height: %d, %d\n", fbW, fbH);
    
    if (first) {
        first = FALSE;
    } else {
        free(cl->frameBuffer);
    }
    pixel_buffer_size = BYTES_PER_PIXEL*fbW*fbH*sizeof(char);
    cl->frameBuffer = (uint8_t*)malloc(pixel_buffer_size);
    framebuffer_resize_callback(fbW, fbH);
    update(cl, 0, 0, fbW, fbH);
    return TRUE;
}

void disconnectVnc() {
    maintainConnection = false;
    SendFramebufferUpdateRequest(cl, 0, 0, 1, 1, FALSE);
}

void connectVnc(void (*callback)(uint8_t *, int fbW, int fbH, int x, int y, int w, int h),
                void (*callback2)(int fbW, int fbH),
                void (*callback3)(void),
                char* addr, char* user, char* password, char* ca_path) {
    printf("Setting up connection.\n");
    maintainConnection = true;
    
    HOST_AND_PORT = addr;
    USERNAME = user;
    PASSWORD = password;
    CA_PATH = ca_path;
    framebuffer_update_callback = callback;
    framebuffer_resize_callback = callback2;
    failure_callback = callback3;

    if(cl != NULL) {
        rfbClientCleanup(cl);
    }

    cl = NULL;

    int argc = 2;
    
    char **argv = (char**)malloc(argc*sizeof(char*));
    int i = 0;
    for (i = 0; i < argc; i++) {
        printf("%d\n", i);
        argv[i] = (char*)malloc(256*sizeof(char));
    }
    strcpy(argv[0], "dummy");
    strcpy(argv[1], HOST_AND_PORT);
    
    /* 16-bit: cl=rfbGetClient(5,3,2); */
    cl=rfbGetClient(8,3,BYTES_PER_PIXEL);
    cl->MallocFrameBuffer=resize;
    cl->canHandleNewFBSize = TRUE;
    cl->GotFrameBufferUpdate=update;
    //cl->HandleKeyboardLedState=kbd_leds;
    //cl->HandleTextChat=text_chat;
    //cl->GotXCutText = got_selection;
    cl->GetCredential = get_credential;
    cl->GetPassword = get_password;
    //cl->listenPort = LISTEN_PORT_OFFSET;
    //cl->listen6Port = LISTEN_PORT_OFFSET;
    
    if (!rfbInitClient(cl, &argc, argv)) {
        cl = NULL; /* rfbInitClient has already freed the client struct */
        cleanup("Failed to initialize RFB Client object.\n\n", cl);
    }
    
    while (cl != NULL) {
        i = WaitForMessage(cl, 1000);
        if (maintainConnection != true) {
            cleanup("Quitting because maintainConnection was set to false.\n\n", cl);
            break;
        }
        if (i < 0) {
            cleanup("Quitting because WaitForMessage < 0\n\n", cl);
            break;
        }
        if (i) {
            printf("Handling RFB Server Message\n\n");
        }
        
        if (!HandleRFBServerMessage(cl)) {
            cleanup("Quitting because HandleRFBServerMessage returned false\n\n", cl);
            break;
        }
    }
    printf("Background thread exiting connectVnc function.\n\n");
}

void cleanup(char *message, rfbClient *client) {
    maintainConnection = false;
    printf("%s", message);
    failure_callback();
}

// TODO: Replace with real conversion table
struct { char mask; int bits_stored; } utf8Mapping[]= {
        {0b00111111, 6},
        {0b01111111, 7},
        {0b00011111, 5},
        {0b00001111, 4},
        {0b00000111, 3},
        {0,0}
};

/* UTF-8 decoding is from https://rosettacode.org/wiki/UTF-8_encode_and_decode which is under GFDL 1.2 */
static rfbKeySym utf8char2rfbKeySym(const char chr[4]) {
        int bytes = (int)strlen(chr);
        int shift = utf8Mapping[0].bits_stored * (bytes - 1);
        rfbKeySym codep = (*chr++ & utf8Mapping[bytes].mask) << shift;
        int i;
        for(i = 1; i < bytes; ++i, ++chr) {
                shift -= utf8Mapping[0].bits_stored;
                codep |= ((char)*chr & utf8Mapping[0].mask) << shift;
        }
        return codep;
}

void sendKeyEvent(const unsigned char *c) {
    if (!maintainConnection) {
        return;
    }
    rfbKeySym sym = utf8char2rfbKeySym(c);
    if (sym == 10) {
        sym = 0xff0d;
    }
    printf("Converted %d to xkeysym: %d\n", (int)*c, sym);
    sendKeyEventWithKeySym(sym);
}

void sendKeyEventWithKeySym(int sym) {
    if (!maintainConnection) {
        return;
    }
    if (cl != NULL) {
        printf("Sending xkeysym: %d\n", sym);
        checkForError(SendKeyEvent(cl, sym, TRUE));
        checkForError(SendKeyEvent(cl, sym, FALSE));
    } else {
        printf("RFB Client object is NULL, need to quit!");
        checkForError(false);
    }
}

void sendPointerEventToServer(int totalX, int totalY, int x, int y, bool firstDown, bool secondDown, bool thirdDown,
                              bool scrollUp, bool scrollDown) {
    if (!maintainConnection) {
        return;
    }
    int buttonMask = 0;
    if (firstDown) {
        buttonMask = buttonMask | rfbButton1Mask;
    }
    if (secondDown) {
        buttonMask = buttonMask | rfbButton2Mask;
    }
    if (thirdDown) {
        buttonMask = buttonMask | rfbButton3Mask;
    }
    if (scrollUp) {
        buttonMask = buttonMask | rfbButton4Mask;
    }
    if (scrollDown) {
        buttonMask = buttonMask | rfbButton5Mask;
    }
    if (cl != NULL) {
        int remoteX = fbW * x / totalX;
        int remoteY = fbH * y / totalY;
        printf("Sending pointer event at %d, %d, with mask %d\n", remoteX, remoteY, buttonMask);
        checkForError(SendPointerEvent(cl, remoteX, remoteY, buttonMask));
    } else {
        printf("RFB Client object is NULL, will quit now.\n");
        checkForError(false);
    }
}

void checkForError(rfbBool res) {
    bool haveQuit = false;
    if (cl == NULL) {
        printf("RFB Client object is NULL, quitting.\n");
        maintainConnection = false;
        failure_callback();
        haveQuit = true;
    }
    if (!res && !haveQuit) {
        printf("Failed to send message, quitting.\n");
        maintainConnection = false;
        failure_callback();
    }
}
