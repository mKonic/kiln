#pragma once

// Person (androidx core Person, android.app.Person): someone in a conversation, for conversation
// shortcuts and MessagingStyle notifications.

#include <string>

namespace kiln::ui {

    struct Person {
        std::string name;
        // Tells apart people with the same name; a stable id from the app's own data.
        std::string key;
        // A contact ("content://com.android.contacts/contacts/lookup/...", "mailto:", "tel:").
        std::string uri;
        // A drawable or mipmap in the app's res/ by name; empty: none.
        std::string icon;
        bool bot = false;         // an automated participant
        bool important = false;   // one the user talks to often
        bool operator==(const Person&) const = default;
    };

}
