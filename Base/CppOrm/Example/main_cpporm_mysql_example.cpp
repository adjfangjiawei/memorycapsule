// Example/main_cpporm_mysql_example.cpp
#include <QCoreApplication>
#include <QDebug>
#include <iostream>
#include <memory>
#include <vector>

#include "cpporm/db_manager.h"
#include "cpporm/model_base.h"
#include "cpporm/query_builder.h"
#include "cpporm/session.h"
#include "sqldriver/mysql/mysql_specific_driver.h"
#include "user_model.h"

cpporm::DbConfig getMySqlConfig() {
    cpporm::DbConfig config;
    config.driver_type = "MYSQL";
    config.host_name = "127.0.0.1";
    config.port = 3306;
    config.database_name = "test_cppgorm_examples";
    config.user_name = "user";
    config.password = "123456789adj";
    config.client_charset = "utf8mb4";
    return config;
}

void runCrudOperations(cpporm::Session& session) {
    qDebug() << "\n--- Running CRUD Operations ---";

    qDebug() << "\n1. Creating users...";
    User user1;
    user1.name = "Alice Wonderland";
    user1.age = 30;
    user1.email = "alice.wonderland@example.com";

    User user2;
    user2.name = "Bob The Builder";
    user2.age = 45;
    user2.email = "bob.builder@example.com";

    auto create_res1 = session.Create(user1);
    if (create_res1) {
        qDebug() << "Created user1, ID:" << user1.id;
        user1.print();
    } else {
        qCritical() << "Failed to create user1:" << QString::fromStdString(create_res1.error().toString());
        return;
    }

    auto create_res2 = session.Create(user2);
    if (create_res2) {
        qDebug() << "Created user2, ID:" << user2.id;
        user2.print();
    } else {
        qCritical() << "Failed to create user2:" << QString::fromStdString(create_res2.error().toString());
    }

    User user3_dup_email;
    user3_dup_email.name = "Charlie Chaplin";
    user3_dup_email.age = 50;
    user3_dup_email.email = "alice.wonderland@example.com";
    auto create_res3 = session.Create(user3_dup_email);
    if (create_res3) {
        qWarning() << "Unexpected: Created user3 with duplicate email. ID:" << user3_dup_email.id;
    } else {
        qInfo() << "Correctly failed to create user3 with duplicate email:" << QString::fromStdString(create_res3.error().toString());
        if (create_res3.error().code == cpporm::ErrorCode::QueryExecutionError || create_res3.error().message.find("Duplicate entry") != std::string::npos || create_res3.error().message.find("UNIQUE constraint failed") != std::string::npos || create_res3.error().native_db_error_code == 1062) {
            qInfo() << "Error indicates constraint violation as expected.";
        }
    }

    qDebug() << "\n2. Reading user with ID:" << user1.id;
    User foundUser1;
    cpporm::Error err = session.First(&foundUser1, user1.id);
    if (!err) {
        qDebug() << "Found user by ID:";
        foundUser1.print();
    } else {
        qCritical() << "Failed to find user by ID " << user1.id << ":" << QString::fromStdString(err.toString());
    }

    qDebug() << "\nReading user with name 'Bob The Builder':";
    User foundUserBob;
    err = session.Model<User>().Where("name = ?", {cpporm::QueryValue(std::string("Bob The Builder"))}).First(&foundUserBob);
    if (!err) {
        qDebug() << "Found user by name:";
        foundUserBob.print();
    } else {
        qWarning() << "Failed to find user by name 'Bob The Builder':" << QString::fromStdString(err.toString());
    }

    qDebug() << "\n3. Updating Alice's age...";
    if (foundUser1.id > 0) {
        foundUser1.age = 31;
        auto save_res = session.Save(foundUser1);
        if (save_res) {
            qDebug() << "Alice updated. Affected rows/status:" << save_res.value();
            User updatedAlice;
            if (!session.First(&updatedAlice, foundUser1.id)) {
                qDebug() << "Alice after update:";
                updatedAlice.print();
            } else {
                qWarning() << "Failed to re-fetch Alice after update.";
            }
        } else {
            qCritical() << "Failed to update Alice:" << QString::fromStdString(save_res.error().toString());
        }
    }

    qDebug() << "\nUpdating age for users older than 40...";
    auto update_res = session.Model<User>().Where("age > ?", {40}).Updates({{"age", cpporm::QueryValue(55)}});
    if (update_res) {
        qDebug() << "Mass update completed. Rows affected:" << update_res.value();
    } else {
        qWarning() << "Mass update failed:" << QString::fromStdString(update_res.error().toString());
    }

    qDebug() << "\n4. Finding all users...";
    std::vector<User> allUsers;
    err = session.Find(&allUsers);
    if (!err) {
        qDebug() << "Found" << allUsers.size() << "users:";
        for (const auto& user : allUsers) {
            user.print();
        }
    } else {
        qCritical() << "Failed to find all users:" << QString::fromStdString(err.toString());
    }

    qDebug() << "\nFinding users with age 55 (using unique_ptr)...";
    std::vector<std::unique_ptr<User>> usersAge55;
    err = session.Model<User>().Where("age = ?", {55}).Find(&usersAge55);
    if (!err) {
        qDebug() << "Found" << usersAge55.size() << "users with age 55:";
        for (const auto& user_ptr : usersAge55) {
            if (user_ptr) user_ptr->print();
        }
    } else {
        qWarning() << "Failed to find users with age 55:" << QString::fromStdString(err.toString());
    }

    qDebug() << "\n5. Deleting Bob (original ID: " << user2.id << ", current model may be different after updates)...";
    User bobForDelete;
    cpporm::Error findBobErr = session.Model<User>().Where("email = ?", {cpporm::QueryValue(std::string("bob.builder@example.com"))}).First(&bobForDelete);

    if (!findBobErr && bobForDelete.id > 0) {
        qDebug() << "Found Bob for deletion, ID: " << bobForDelete.id;
        // auto delete_res = session.Delete(bobForDelete);
        // if (delete_res) {
        //     qDebug() << "Bob deleted. Rows affected:" << delete_res.value();
        // } else {
        //     qCritical() << "Failed to delete Bob:" << QString::fromStdString(delete_res.error().toString());
        // }
        // User deletedBobCheck;
        // err = session.First(&deletedBobCheck, bobForDelete.id);
        // if (err && err.code == cpporm::ErrorCode::RecordNotFound) {
        //     qInfo() << "Bob (ID: " << bobForDelete.id << ") correctly not found after deletion.";
        // } else if (!err) {
        //     qWarning() << "Unexpected: Bob (ID: " << bobForDelete.id << ") found after attempting deletion.";
        // } else {
        //     qWarning() << "Error checking for Bob after deletion:" << QString::fromStdString(err.toString());
        // }
    } else {
        qWarning() << "Could not find Bob by email for deletion. Original ID was" << user2.id << ". Error:" << QString::fromStdString(findBobErr.toString());
    }

    qDebug() << "\n6. Counting remaining users...";
    auto count_res = session.Model<User>().Count();
    if (count_res) {
        qDebug() << "Number of users remaining:" << count_res.value();
    } else {
        qWarning() << "Failed to count users:" << QString::fromStdString(count_res.error().toString());
    }
}

void runTransactionExample(cpporm::Session& main_session) {
    qDebug() << "\n--- Running Transaction Example ---";

    auto tx_session_expected = main_session.Begin();
    if (!tx_session_expected) {
        qCritical() << "Failed to begin transaction:" << QString::fromStdString(tx_session_expected.error().toString());
        return;
    }
    std::unique_ptr<cpporm::Session> tx_session = std::move(tx_session_expected.value());
    qDebug() << "Transaction started on a new Session wrapper (original session still usable).";

    User user_tx1;
    user_tx1.name = "Tx User One";
    user_tx1.age = 70;
    user_tx1.email = "tx.user.one@example.com";

    auto create_tx1_res = tx_session->Create(user_tx1);
    if (!create_tx1_res) {
        qCritical() << "Failed to create user in transaction:" << QString::fromStdString(create_tx1_res.error().toString());
        cpporm::Error rollback_err = tx_session->Rollback();
        if (rollback_err) {
            qWarning() << "Failed to rollback transaction after error:" << QString::fromStdString(rollback_err.toString());
        } else {
            qDebug() << "Transaction rolled back due to error.";
        }
        return;
    }
    qDebug() << "Created user_tx1 (ID:" << user_tx1.id << ") inside transaction.";

    bool simulate_error = true;
    cpporm::Error tx_op_err;

    if (simulate_error) {
        qDebug() << "Simulating an error, rolling back transaction...";
        tx_op_err = tx_session->Rollback();
        if (tx_op_err) {
            qCritical() << "Failed to rollback transaction:" << QString::fromStdString(tx_op_err.toString());
        } else {
            qDebug() << "Transaction rolled back successfully.";
        }
    } else {
        qDebug() << "Committing transaction...";
        tx_op_err = tx_session->Commit();
        if (tx_op_err) {
            qCritical() << "Failed to commit transaction:" << QString::fromStdString(tx_op_err.toString());
        } else {
            qDebug() << "Transaction committed successfully.";
        }
    }

    // Now, main_session should still be usable as it shares the ISqlDriver.
    User check_tx_user;
    cpporm::Error err_tx_check = main_session.Model<User>().Where("id = ?", {user_tx1.id}).First(&check_tx_user);
    if (simulate_error) {
        if (err_tx_check && err_tx_check.code == cpporm::ErrorCode::RecordNotFound) {
            qInfo() << "User_tx1 (ID:" << user_tx1.id << ") correctly not found after rollback (checked with original session).";
        } else if (!err_tx_check) {
            qWarning() << "Unexpected: User_tx1 (ID:" << user_tx1.id << ") found after rollback (checked with original session)!";
            check_tx_user.print();
        } else {
            qWarning() << "Error checking for user_tx1 after rollback (with original session):" << QString::fromStdString(err_tx_check.toString());
        }
    } else {
        if (!err_tx_check) {
            qInfo() << "User_tx1 (ID:" << user_tx1.id << ") found after commit, as expected (checked with original session).";
            check_tx_user.print();
        } else {
            qWarning() << "User_tx1 (ID:" << user_tx1.id << ") not found after commit or other error (checked with original session):" << QString::fromStdString(err_tx_check.toString());
        }
    }
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    cpporm_sqldriver::MySqlDriver_Initialize();
    qDebug() << "CppOrm MySQL Example Starting...";
    cpporm::finalize_all_model_meta();
    qDebug() << "Model metadata finalized.";

    cpporm::DbConfig db_config = getMySqlConfig();

    auto db_expected = cpporm::DbManager::openDatabase(db_config);
    if (!db_expected) {
        qCritical() << "Failed to open database:" << QString::fromStdString(db_expected.error().toString());
        return -1;
    }
    cpporm::Session session(std::move(db_expected.value()));

    if (!session.getDbHandle().isOpen()) {
        qCritical() << "Session could not use the database connection (it's not open). Last DB error in session handle:" << QString::fromStdString(session.getDbHandle().lastError().text());
        return -1;
    }
    qDebug() << "Database connection" << QString::fromStdString(session.getConnectionName()) << "opened and session created successfully.";

    qDebug() << "\n--- Running AutoMigration for User model ---";
    cpporm::Error migrate_err = session.AutoMigrate(User::getModelMeta());
    if (migrate_err) {
        qWarning() << "AutoMigration for User model failed:" << QString::fromStdString(migrate_err.toString());
    } else {
        qDebug() << "AutoMigration for User model completed.";
    }

    runCrudOperations(session);
    runTransactionExample(session);  // Original session should remain usable

    // if (session.getDbHandle().isOpen()) {
    //     qDebug() << "\n--- Cleaning up ---";
    //     auto cleanup_res = session.Model<User>().Where("1=1").Delete();
    //     if (cleanup_res) {
    //         qDebug() << "Cleaned up users table. Rows affected:" << cleanup_res.value();
    //     } else {
    //         qWarning() << "Failed to clean up users table:" << QString::fromStdString(cleanup_res.error().toString());
    //     }
    // } else {
    //     qCritical() << "Main session db_handle is no longer open after transaction example. Cleanup cannot proceed.";
    //     // This indicates a problem if the shared_ptr logic didn't work as expected,
    //     // or if a transaction error caused the underlying connection to close.
    // }

    qDebug() << "Database connection" << QString::fromStdString(session.getConnectionName()) << " will be closed when session goes out of scope.";
    qDebug() << "CppOrm MySQL Example Finished.";

    return 0;
}