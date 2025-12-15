/* banking_project_with_loan.c
   Full Banking Transaction Management System with Loan Module
   Features:
   - Accounts stored in BST
   - Transaction history (linked list per account)
   - Undo / Redo (stacks)
   - Customer service queue (circular queue using linked nodes)
   - Loan subsystem:
       * Apply loan (user inputs interest + choose simple/compound)
       * EMI calculation (monthly amortization if chosen)
       * Loan ID, loan type, interest type stored per loan
       * No loan limit (as requested)
       * Pay loan (partial/full), loan status, loan history integrated
   - All original operations preserved: deposit, withdraw, transfer, print, update, delete
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ===================== CONSTANTS & HELPERS ===================== */
#define NAME_SIZE 50
#define TYPE_SIZE 40
#define MAX_LINE 256

void flushInput() {
    int c;
    while ((c = getchar()) != '\n' && c != EOF) { }
}

/* ===================== PART A: Data Structures ===================== */

/* Transaction history (simple linked list stored per account) */
typedef struct Transaction {
    char type[TYPE_SIZE];
    int amount;
    int otherAcc; // -1 if not applicable
    struct Transaction* next;
} Transaction;

/* Loan record (per account) */
typedef enum { LOAN_SIMPLE = 0, LOAN_COMPOUND = 1 } LoanInterestType;
typedef enum { LOAN_ACTIVE = 0, LOAN_CLOSED = 1 } LoanStatus;

typedef struct Loan {
    int loanID;
    int principal;           // principal amount
    double interestRate;     // annual interest rate in decimal (e.g., 0.10 for 10%)
    LoanInterestType itype;  // simple or compound interest
    int termMonths;          // duration in months
    double emi;              // monthly EMI if amortized (0 if not using EMI)
    double remaining;        // remaining amount to pay (principal + interest as applicable)
    LoanStatus status;
    char loanType[TYPE_SIZE]; // e.g., "Personal", "Auto", etc.
    struct Loan* next;
} Loan;

/* Account stored in BST nodes */
typedef struct Account {
    int accNo;
    char name[NAME_SIZE];
    int balance;
    Transaction* history;
    Loan* loans; // linked list of loans for this account
    struct Account* left;
    struct Account* right;
} Account;

/* ---------------- Undo/Redo Action ---------------- */
typedef enum {
    ACT_DEPOSIT,
    ACT_WITHDRAW,
    ACT_TRANSFER,
    ACT_CREATE,
    ACT_DELETE,
    ACT_LOAN_APPLY,
    ACT_LOAN_PAYMENT,
    ACT_LOAN_CLOSE
} ActionType;

typedef struct Action {
    ActionType type;
    int accNo1;
    int accNo2; // for transfer
    int amount;
    char name[NAME_SIZE]; // for create/delete or loan metadata storage if needed
    int loanID;           // for loan related actions
    double extra;         // used to store EMI or interest snapshot or remaining amount (when needed)
    int balanceSnapshot;  // snapshot of balance if needed
    struct Action* next;
} Action;

/* Undo / Redo stacks */
Action* undoTop = NULL;
Action* redoTop = NULL;

/* ---------------- Customer Queue (simple linked queue) ---------------- */
typedef struct QueueNode {
    int accNo;
    struct QueueNode* next;
} QueueNode;

QueueNode* qFront = NULL;
QueueNode* qRear = NULL;

/* Global loan ID generator */
int globalLoanID = 1000;

/* ===================== PART B: Function Declarations ===================== */

/* Account BST functions */
Account* createAccountNode(int accNo, const char* name);
Account* insertAccount(Account* root, int accNo, const char* name);
Account* searchAccount(Account* root, int accNo);
Account* findMin(Account* root);
Account* deleteAccount(Account* root, int accNo, Account* snapshot);
void updateAccount(Account* root);
void createNewAccount(Account** rootPtr);

/* Transaction functions */
void addTransaction(Account* acc, const char* type, int amount, int otherAcc);
void deposit(Account* root);
void withdraw(Account* root);
void transferMoney(Account* root);

/* Loan functions */
Loan* createLoanRecord(int principal, double annualRate, LoanInterestType itype, int termMonths, const char* loanType);
void applyLoan(Account* root);
void payLoan(Account* root);
Loan* findLoan(Account* acc, int loanID);
void printLoanDetails(Loan* loan);
void printAllLoans(Account* acc);

/* Undo / Redo functions */
void pushAction(Action** top, Action action);
int popAction(Action** top, Action* out);
void clearStack(Action** top);
void recordAction(ActionType type, int accNo1, int accNo2, int amount, const char* name, int loanID, double extra, int balanceSnapshot);
void undoOperation(Account** rootPtr);
void redoOperation(Account** rootPtr);

/* Queue functions */
void enqueueCustomer(int accNo);
void serveCustomer();

/* Reporting */
void printAccountDetails(Account* acc);
void printAllAccountsInOrder(Account* root);

/* Utility */
void printMainMenu();

/* ===================== PART C: Implementation ===================== */

/* -------- Account BST -------- */

Account* createAccountNode(int accNo, const char* name) {
    Account* a = (Account*)malloc(sizeof(Account));
    if (!a) {
        printf("Memory allocation failed!\n");
        exit(1);
    }
    a->accNo = accNo;
    strncpy(a->name, name, NAME_SIZE - 1);
    a->name[NAME_SIZE - 1] = '\0';
    a->balance = 0;
    a->history = NULL;
    a->loans = NULL;
    a->left = a->right = NULL;
    return a;
}

Account* insertAccount(Account* root, int accNo, const char* name) {
    if (root == NULL)
        return createAccountNode(accNo, name);

    if (accNo < root->accNo)
        root->left = insertAccount(root->left, accNo, name);
    else if (accNo > root->accNo)
        root->right = insertAccount(root->right, accNo, name);
    else
        printf("Account %d already exists.\n", accNo);

    return root;
}

Account* searchAccount(Account* root, int accNo) {
    if (root == NULL || root->accNo == accNo)
        return root;
    if (accNo < root->accNo)
        return searchAccount(root->left, accNo);
    else
        return searchAccount(root->right, accNo);
}

Account* findMin(Account* root) {
    while (root && root->left)
        root = root->left;
    return root;
}

/* snapshot is optional; if not NULL fill it for undo */
Account* deleteAccount(Account* root, int accNo, Account* snapshot) {
    if (root == NULL)
        return NULL;

    if (accNo < root->accNo) {
        root->left = deleteAccount(root->left, accNo, snapshot);
    } else if (accNo > root->accNo) {
        root->right = deleteAccount(root->right, accNo, snapshot);
    } else {
        /* Found node */
        if (snapshot) {
            snapshot->accNo = root->accNo;
            strncpy(snapshot->name, root->name, NAME_SIZE - 1);
            snapshot->name[NAME_SIZE - 1] = '\0';
            snapshot->balance = root->balance;
            snapshot->history = root->history; // shallow copy pointer
            snapshot->loans = root->loans;     // shallow copy pointer (for undo restore we will not deep copy)
        }

        if (root->left == NULL) {
            Account* temp = root->right;
            // NOTE: not freeing transactions and loans here to avoid losing snapshot pointers used by undo;
            free(root);
            return temp;
        } else if (root->right == NULL) {
            Account* temp = root->left;
            free(root);
            return temp;
        } else {
            Account* temp = findMin(root->right);
            root->accNo = temp->accNo;
            strncpy(root->name, temp->name, NAME_SIZE - 1);
            root->name[NAME_SIZE - 1] = '\0';
            root->balance = temp->balance;
            root->history = temp->history; // shallow move
            root->loans = temp->loans;     // shallow move
            root->right = deleteAccount(root->right, temp->accNo, NULL);
        }
    }
    return root;
}

void createNewAccount(Account** rootPtr) {
    int accNo;
    char name[NAME_SIZE];

    printf("Enter New Account Number: ");
    if (scanf("%d", &accNo) != 1) {
        printf("Invalid input.\n");
        flushInput();
        return;
    }

    if (searchAccount(*rootPtr, accNo) != NULL) {
        printf("Account already exists!\n");
        return;
    }

    printf("Enter Account Holder Name: ");
    flushInput();
    if (!fgets(name, sizeof(name), stdin)) {
        printf("Error reading name.\n");
        return;
    }
    name[strcspn(name, "\n")] = '\0';

    *rootPtr = insertAccount(*rootPtr, accNo, name);
    Account* acc = searchAccount(*rootPtr, accNo);

    // Mandatory initial deposit = 700
    acc->balance = 700;
    addTransaction(acc, "Initial Deposit (Mandatory)", 700, -1);

    recordAction(ACT_CREATE, accNo, -1, 0, name, -1, 0.0, acc->balance);
    clearStack(&redoTop);

    printf("Account created successfully! Initial balance: 700 Tk (Mandatory)\n");
}

void updateAccount(Account* root) {
    int accNo;
    printf("Enter account number to update: ");
    if (scanf("%d", &accNo) != 1) {
        printf("Invalid input.\n");
        flushInput();
        return;
    }

    Account* acc = searchAccount(root, accNo);
    if (!acc) {
        printf("Account not found.\n");
        return;
    }

    printf("Current name: %s\n", acc->name);
    printf("Enter new name: ");
    flushInput();
    char newName[NAME_SIZE];
    if (!fgets(newName, sizeof(newName), stdin)) {
        printf("Error reading name.\n");
        return;
    }
    newName[strcspn(newName, "\n")] = '\0';

    strncpy(acc->name, newName, NAME_SIZE - 1);
    acc->name[NAME_SIZE - 1] = '\0';

    printf("Account updated successfully.\n");
}

/* -------- Transaction linked list functions -------- */

void addTransaction(Account* acc, const char* type, int amount, int otherAcc) {
    if (!acc) return;
    Transaction* t = (Transaction*)malloc(sizeof(Transaction));
    if (!t) {
        printf("Memory allocation failed!\n");
        exit(1);
    }
    strncpy(t->type, type, TYPE_SIZE - 1);
    t->type[TYPE_SIZE - 1] = '\0';
    t->amount = amount;
    t->otherAcc = otherAcc;
    t->next = acc->history;
    acc->history = t;
}

/* deposit/withdraw/transfer */

void deposit(Account* root) {
    int accNo, amount;
    printf("Enter account number: ");
    if (scanf("%d", &accNo) != 1) {
        printf("Invalid input.\n");
        flushInput();
        return;
    }
    Account* acc = searchAccount(root, accNo);
    if (!acc) {
        printf("Account not found.\n");
        return;
    }
    printf("Enter amount to deposit: ");
    if (scanf("%d", &amount) != 1 || amount <= 0) {
        printf("Invalid amount.\n");
        flushInput();
        return;
    }
    acc->balance += amount;
    addTransaction(acc, "Deposit", amount, -1);

    recordAction(ACT_DEPOSIT, accNo, -1, amount, "", -1, 0.0, acc->balance - amount);
    clearStack(&redoTop);

    printf("Deposit successful. New balance: %d\n", acc->balance);
}

void withdraw(Account* root) {
    int accNo, amount;
    printf("Enter account number: ");
    if (scanf("%d", &accNo) != 1) {
        printf("Invalid input.\n");
        flushInput();
        return;
    }

    Account* acc = searchAccount(root, accNo);
    if (!acc) {
        printf("Account not found.\n");
        return;
    }

    printf("Enter amount to withdraw: ");
    if (scanf("%d", &amount) != 1 || amount <= 0) {
        printf("Invalid amount.\n");
        flushInput();
        return;
    }

    // RULE 1: Minimum withdraw = 500
    if (amount < 500) {
        printf("Minimum withdraw amount is 500 Tk.\n");
        return;
    }

    // RULE 2: After withdraw, balance must be >= 700
    if (acc->balance - amount < 700) {
        printf("You must keep at least 700 Tk in your account.\n");
        return;
    }

    acc->balance -= amount;
    addTransaction(acc, "Withdraw", amount, -1);

    recordAction(ACT_WITHDRAW, accNo, -1, amount, "", -1, 0.0, acc->balance + amount);
    clearStack(&redoTop);

    printf("Withdraw successful. New balance: %d\n", acc->balance);
}

void transferMoney(Account* root) {
    int fromAccNo, toAccNo, amount;
    printf("Enter FROM account number: ");
    if (scanf("%d", &fromAccNo) != 1) {
        printf("Invalid input.\n");
        flushInput();
        return;
    }
    printf("Enter TO account number: ");
    if (scanf("%d", &toAccNo) != 1) {
        printf("Invalid input.\n");
        flushInput();
        return;
    }
    if (fromAccNo == toAccNo) {
        printf("Cannot transfer to same account.\n");
        return;
    }

    Account* fromAcc = searchAccount(root, fromAccNo);
    Account* toAcc = searchAccount(root, toAccNo);

    if (!fromAcc || !toAcc) {
        printf("One or both accounts not found.\n");
        return;
    }

    printf("Enter amount to transfer: ");
    if (scanf("%d", &amount) != 1 || amount <= 0) {
        printf("Invalid amount.\n");
        flushInput();
        return;
    }
    if (fromAcc->balance < amount) {
        printf("Insufficient balance in FROM account.\n");
        return;
    }

    fromAcc->balance -= amount;
    toAcc->balance += amount;

    char buf[TYPE_SIZE];
    snprintf(buf, sizeof(buf), "Transfer to %d", toAccNo);
    addTransaction(fromAcc, buf, amount, toAccNo);

    snprintf(buf, sizeof(buf), "Transfer from %d", fromAccNo);
    addTransaction(toAcc, buf, amount, fromAccNo);

    recordAction(ACT_TRANSFER, fromAccNo, toAccNo, amount, "", -1, 0.0, 0);
    clearStack(&redoTop);

    printf("Transfer successful.\n");
}

/* -------- Loan subsystem -------- */

/* EMI calculation for amortizing loan:
   EMI = P * r * (1+r)^n / ((1+r)^n - 1)
   where r = monthly interest rate (annualRate/12), n = months
*/
double calculateEMI(double principal, double annualRate, int termMonths) {
    if (termMonths <= 0) return 0.0;
    double monthlyRate = annualRate / 12.0;
    if (monthlyRate <= 0.0) {
        return principal / termMonths;
    }
    double r = monthlyRate;
    double numerator = principal * r * pow(1 + r, termMonths);
    double denom = pow(1 + r, termMonths) - 1.0;
    if (denom == 0.0) return principal / termMonths;
    return numerator / denom;
}

Loan* createLoanRecord(int principal, double annualRate, LoanInterestType itype, int termMonths, const char* loanType) {
    Loan* L = (Loan*)malloc(sizeof(Loan));
    if (!L) {
        printf("Memory allocation failed for loan!\n");
        exit(1);
    }
    L->loanID = globalLoanID++;
    L->principal = principal;
    L->interestRate = annualRate;
    L->itype = itype;
    L->termMonths = termMonths;
    L->status = LOAN_ACTIVE;
    strncpy(L->loanType, loanType ? loanType : "General", TYPE_SIZE - 1);
    L->loanType[TYPE_SIZE - 1] = '\0';
    L->next = NULL;

    // For simple interest: remaining = principal + principal * rate * (termYears)
    if (itype == LOAN_SIMPLE) {
        double years = termMonths / 12.0;
        L->remaining = principal + (principal * annualRate * years);
        L->emi = calculateEMI(L->remaining, 0.0, termMonths); // monthly without additional compounding
    } else { // compound interest -> we will treat as amortized loan using EMI formula on principal with annualRate
        L->emi = calculateEMI(principal, annualRate, termMonths);
        L->remaining = L->emi * termMonths; // total payment over term
    }
    return L;
}

/* find loan by id in account */
Loan* findLoan(Account* acc, int loanID) {
    if (!acc) return NULL;
    Loan* cur = acc->loans;
    while (cur) {
        if (cur->loanID == loanID) return cur;
        cur = cur->next;
    }
    return NULL;
}

void applyLoan(Account* root) {
    int accNo;
    printf("Enter account number to apply loan: ");
    if (scanf("%d", &accNo) != 1) {
        printf("Invalid input.\n");
        flushInput();
        return;
    }
    Account* acc = searchAccount(root, accNo);
    if (!acc) {
        printf("Account not found.\n");
        return;
    }

    int principal;
    double annualRate;
    int itypeChoice;
    int termMonths;
    char loanType[TYPE_SIZE];

    printf("Enter loan type (e.g., Personal, Auto): ");
    flushInput();
    if (!fgets(loanType, sizeof(loanType), stdin)) return;
    loanType[strcspn(loanType, "\n")] = '\0';

    printf("Enter principal amount: ");
    if (scanf("%d", &principal) != 1 || principal <= 0) {
        printf("Invalid principal.\n");
        flushInput();
        return;
    }

    printf("Enter annual interest rate (e.g., 0.10 for 10%%): ");
    if (scanf("%lf", &annualRate) != 1 || annualRate < 0.0) {
        printf("Invalid interest rate.\n");
        flushInput();
        return;
    }

    printf("Choose interest calculation type: 0 -> Simple, 1 -> Compound (amortized EMI): ");
    if (scanf("%d", &itypeChoice) != 1 || (itypeChoice != 0 && itypeChoice != 1)) {
        printf("Invalid choice.\n");
        flushInput();
        return;
    }

    printf("Enter term in months (e.g., 12 for 1 year): ");
    if (scanf("%d", &termMonths) != 1 || termMonths <= 0) {
        printf("Invalid term.\n");
        flushInput();
        return;
    }

    LoanInterestType itype = (itypeChoice == 0) ? LOAN_SIMPLE : LOAN_COMPOUND;
    Loan* ln = createLoanRecord(principal, annualRate, itype, termMonths, loanType);

    // Disburse principal to account balance (usual banking behavior)
    acc->balance += principal;

    // Add loan to account's loan list
    ln->next = acc->loans;
    acc->loans = ln;

    // Add transaction record
    addTransaction(acc, "Loan Disbursed", principal, -1);

    // Record action for undo (store loanID and snapshot remaining)
    recordAction(ACT_LOAN_APPLY, accNo, -1, principal, loanType, ln->loanID, ln->remaining, acc->balance - principal);
    clearStack(&redoTop);

    printf("Loan approved! Loan ID: %d\n", ln->loanID);
    printf("Principal credited to account. New balance: %d\n", acc->balance);
    if (ln->itype == LOAN_SIMPLE) {
        printf("Simple interest. Total payable (approx): %.2f Tk over %d months. Monthly (approx): %.2f\n", ln->remaining, ln->termMonths, ln->remaining / ln->termMonths);
    } else {
        printf("EMI loan. Monthly EMI: %.2f Tk for %d months. Total payable (approx): %.2f\n", ln->emi, ln->termMonths, ln->remaining);
    }
}

void payLoan(Account* root) {
    int accNo;
    printf("Enter account number: ");
    if (scanf("%d", &accNo) != 1) {
        printf("Invalid input.\n");
        flushInput();
        return;
    }
    Account* acc = searchAccount(root, accNo);
    if (!acc) {
        printf("Account not found.\n");
        return;
    }
    if (!acc->loans) {
        printf("No loans found for this account.\n");
        return;
    }

    printAllLoans(acc);
    int loanID;
    printf("Enter Loan ID to pay: ");
    if (scanf("%d", &loanID) != 1) {
        printf("Invalid input.\n");
        flushInput();
        return;
    }

    Loan* ln = findLoan(acc, loanID);
    if (!ln) {
        printf("Loan ID not found.\n");
        return;
    }
    if (ln->status == LOAN_CLOSED) {
        printf("This loan is already closed.\n");
        return;
    }

    double payAmount;
    printf("Enter payment amount: ");
    if (scanf("%lf", &payAmount) != 1 || payAmount <= 0.0) {
        printf("Invalid amount.\n");
        flushInput();
        return;
    }
    if (acc->balance < payAmount) {
        printf("Insufficient account balance to make payment.\n");
        return;
    }

    // Deduct from account balance
    acc->balance -= (int)payAmount;

    // Reduce loan remaining
    ln->remaining -= payAmount;
    if (ln->remaining <= 0.0) {
        ln->remaining = 0.0;
        ln->status = LOAN_CLOSED;
    }

    // Add transaction
    addTransaction(acc, "Loan Payment", (int)payAmount, -1);

    // Record action for undo: store loanID, amount paid, and previous remaining in extra
    recordAction(ACT_LOAN_PAYMENT, accNo, -1, (int)payAmount, "", ln->loanID, ln->remaining + payAmount, acc->balance + (int)payAmount);
    clearStack(&redoTop);

    printf("Payment applied. Loan ID %d remaining amount: %.2f\n", ln->loanID, ln->remaining);
    if (ln->status == LOAN_CLOSED) {
        printf("Loan %d fully paid and closed.\n", ln->loanID);
        recordAction(ACT_LOAN_CLOSE, accNo, -1, 0, ln->loanType, ln->loanID, 0.0, acc->balance);
        clearStack(&redoTop);
    }
}

void printLoanDetails(Loan* loan) {
    if (!loan) return;
    printf("LoanID: %d | Type: %s | Principal: %d | InterestRate: %.4f | Term: %d months | EMI: %.2f | Remaining: %.2f | Status: %s | InterestCalc: %s\n",
           loan->loanID, loan->loanType, loan->principal, loan->interestRate, loan->termMonths, loan->emi, loan->remaining,
           (loan->status == LOAN_ACTIVE) ? "Active" : "Closed",
           (loan->itype == LOAN_SIMPLE) ? "Simple" : "Compound(EMI)");
}

void printAllLoans(Account* acc) {
    if (!acc) {
        printf("Account not found.\n");
        return;
    }
    if (!acc->loans) {
        printf("  No loans for this account.\n");
        return;
    }
    printf("  Loans for Account %d:\n", acc->accNo);
    Loan* cur = acc->loans;
    while (cur) {
        printf("   ");
        printLoanDetails(cur);
        cur = cur->next;
    }
}

/* -------- Undo / Redo stack functions -------- */

void pushAction(Action** top, Action action) {
    Action* node = (Action*)malloc(sizeof(Action));
    if (!node) {
        printf("Memory allocation failed!\n");
        exit(1);
    }
    *node = action;
    node->next = *top;
    *top = node;
}

int popAction(Action** top, Action* out) {
    if (*top == NULL)
        return 0;
    Action* temp = *top;
    *out = *temp;
    *top = temp->next;
    free(temp);
    return 1;
}

void clearStack(Action** top) {
    Action tmp;
    while (popAction(top, &tmp)) {
        /* nothing, popped and freed in popAction */
    }
}

void recordAction(ActionType type, int accNo1, int accNo2, int amount, const char* name, int loanID, double extra, int balanceSnapshot) {
    Action action;
    action.type = type;
    action.accNo1 = accNo1;
    action.accNo2 = accNo2;
    action.amount = amount;
    strncpy(action.name, name ? name : "", NAME_SIZE - 1);
    action.name[NAME_SIZE - 1] = '\0';
    action.loanID = loanID;
    action.extra = extra;
    action.balanceSnapshot = balanceSnapshot;
    action.next = NULL;
    pushAction(&undoTop, action);
}

/* Undo operation: try to invert the last action and push inverse into redo stack */
void undoOperation(Account** rootPtr) {
    Action action;
    if (!popAction(&undoTop, &action)) {
        printf("Nothing to undo.\n");
        return;
    }

    Account* acc1;
    Account* acc2;
    Action inverse;

    switch (action.type) {
        case ACT_DEPOSIT:
            acc1 = searchAccount(*rootPtr, action.accNo1);
            if (!acc1) {
                printf("Account not found for undo deposit.\n");
                break;
            }
            acc1->balance -= action.amount;
            addTransaction(acc1, "Undo Deposit", action.amount, -1);

            inverse = action;
            pushAction(&redoTop, inverse);
            printf("Undo deposit successful.\n");
            break;

        case ACT_WITHDRAW:
            acc1 = searchAccount(*rootPtr, action.accNo1);
            if (!acc1) {
                printf("Account not found for undo withdraw.\n");
                break;
            }
            acc1->balance += action.amount;
            addTransaction(acc1, "Undo Withdraw", action.amount, -1);

            inverse = action;
            pushAction(&redoTop, inverse);
            printf("Undo withdraw successful.\n");
            break;

        case ACT_TRANSFER:
            acc1 = searchAccount(*rootPtr, action.accNo1);
            acc2 = searchAccount(*rootPtr, action.accNo2);
            if (!acc1 || !acc2) {
                printf("Accounts not found for undo transfer.\n");
                break;
            }
            if (acc2->balance < action.amount) {
                printf("Cannot undo transfer, target balance too low.\n");
                break;
            }
            acc2->balance -= action.amount;
            acc1->balance += action.amount;
            addTransaction(acc1, "Undo Transfer (back)", action.amount, action.accNo2);
            addTransaction(acc2, "Undo Transfer (reversed)", action.amount, action.accNo1);

            inverse = action;
            pushAction(&redoTop, inverse);
            printf("Undo transfer successful.\n");
            break;

        case ACT_CREATE:
            // Undo account creation -> delete the account
            *rootPtr = deleteAccount(*rootPtr, action.accNo1, NULL);
            inverse = action;
            pushAction(&redoTop, inverse);
            printf("Undo account creation successful.\n");
            break;

        case ACT_DELETE: {
            // Undo deletion -> recreate minimal snapshot
            *rootPtr = insertAccount(*rootPtr, action.accNo1, action.name);
            Account* recreated = searchAccount(*rootPtr, action.accNo1);
            if (recreated) {
                recreated->balance = action.balanceSnapshot;
                // Note: transaction history and loans might be lost unless deeper snapshot implemented
            }
            inverse = action;
            pushAction(&redoTop, inverse);
            printf("Undo account deletion successful.\n");
            break;
        }

        case ACT_LOAN_APPLY: {
            // Undo loan application -> remove loan and subtract credited principal
            acc1 = searchAccount(*rootPtr, action.accNo1);
            if (!acc1) {
                printf("Account not found for undo loan apply.\n");
                break;
            }
            Loan* prev = NULL;
            Loan* cur = acc1->loans;
            while (cur && cur->loanID != action.loanID) {
                prev = cur;
                cur = cur->next;
            }
            if (!cur) {
                printf("Loan not found for undo.\n");
                break;
            }
            // Remove loan from list
            if (prev) prev->next = cur->next;
            else acc1->loans = cur->next;

            // Revert credited principal (safe: subtract principal if balance enough, else allow negative)
            acc1->balance -= cur->principal;

            addTransaction(acc1, "Undo Loan Apply (removed)", cur->principal, -1);

            // push inverse to redo (same loanID and principal)
            inverse = action;
            pushAction(&redoTop, inverse);

            // free loan node
            free(cur);

            printf("Undo loan application successful (loan removed, principal debited back).\n");
            break;
        }

        case ACT_LOAN_PAYMENT: {
            acc1 = searchAccount(*rootPtr, action.accNo1);
            if (!acc1) {
                printf("Account not found for undo loan payment.\n");
                break;
            }
            Loan* ln = findLoan(acc1, action.loanID);
            if (!ln) {
                printf("Loan not found for undo payment.\n");
                break;
            }
            // action.extra stored previous remaining (when recorded we stored previous remaining)
            double prevRemaining = action.extra;
            int paidAmount = action.amount;
            // revert balance and remaining
            acc1->balance += paidAmount;
            ln->remaining = prevRemaining;
            if (ln->remaining > 0.0) ln->status = LOAN_ACTIVE;

            addTransaction(acc1, "Undo Loan Payment", paidAmount, -1);

            inverse = action;
            pushAction(&redoTop, inverse);
            printf("Undo loan payment successful.\n");
            break;
        }

        case ACT_LOAN_CLOSE: {
            // For simplicity, undoing a loan close won't restore payments; we just mark active
            acc1 = searchAccount(*rootPtr, action.accNo1);
            if (!acc1) {
                printf("Account not found for undo loan close.\n");
                break;
            }
            Loan* ln = findLoan(acc1, action.loanID);
            if (!ln) {
                printf("Loan not found for undo loan close.\n");
                break;
            }
            ln->status = LOAN_ACTIVE;
            inverse = action;
            pushAction(&redoTop, inverse);
            printf("Undo loan close: loan marked active again.\n");
            break;
        }

        default:
            printf("Unknown action type for undo.\n");
            break;
    }
}

/* Redo: apply the action popped from redo stack and push inverse to undo */
void redoOperation(Account** rootPtr) {
    Action action;
    if (!popAction(&redoTop, &action)) {
        printf("Nothing to redo.\n");
        return;
    }

    Account* acc1;
    Account* acc2;
    Action inverse;

    switch (action.type) {
        case ACT_DEPOSIT:
            acc1 = searchAccount(*rootPtr, action.accNo1);
            if (!acc1) {
                printf("Account not found for redo deposit.\n");
                break;
            }
            acc1->balance += action.amount;
            addTransaction(acc1, "Redo Deposit", action.amount, -1);
            inverse = action;
            pushAction(&undoTop, inverse);
            printf("Redo deposit successful.\n");
            break;

        case ACT_WITHDRAW:
            acc1 = searchAccount(*rootPtr, action.accNo1);
            if (!acc1) {
                printf("Account not found for redo withdraw.\n");
                break;
            }
            if (acc1->balance < action.amount) {
                printf("Cannot redo withdraw, insufficient balance.\n");
                break;
            }
            acc1->balance -= action.amount;
            addTransaction(acc1, "Redo Withdraw", action.amount, -1);
            inverse = action;
            pushAction(&undoTop, inverse);
            printf("Redo withdraw successful.\n");
            break;

        case ACT_TRANSFER:
            acc1 = searchAccount(*rootPtr, action.accNo1);
            acc2 = searchAccount(*rootPtr, action.accNo2);
            if (!acc1 || !acc2) {
                printf("Accounts not found for redo transfer.\n");
                break;
            }
            if (acc1->balance < action.amount) {
                printf("Cannot redo transfer, insufficient balance.\n");
                break;
            }
            acc1->balance -= action.amount;
            acc2->balance += action.amount;
            addTransaction(acc1, "Redo Transfer (to)", action.amount, action.accNo2);
            addTransaction(acc2, "Redo Transfer (from)", action.amount, action.accNo1);
            inverse = action;
            pushAction(&undoTop, inverse);
            printf("Redo transfer successful.\n");
            break;

        case ACT_CREATE:
            *rootPtr = insertAccount(*rootPtr, action.accNo1, action.name);
            acc1 = searchAccount(*rootPtr, action.accNo1);
            if (acc1) {
                acc1->balance = action.balanceSnapshot;
                if (action.balanceSnapshot > 0) addTransaction(acc1, "Redo Initial Balance", action.balanceSnapshot, -1);
            }
            inverse = action;
            pushAction(&undoTop, inverse);
            printf("Redo account creation successful.\n");
            break;

        case ACT_DELETE:
            *rootPtr = deleteAccount(*rootPtr, action.accNo1, NULL);
            inverse = action;
            pushAction(&undoTop, inverse);
            printf("Redo account deletion successful.\n");
            break;

        case ACT_LOAN_APPLY: {
            acc1 = searchAccount(*rootPtr, action.accNo1);
            if (!acc1) {
                printf("Account not found for redo loan apply.\n");
                break;
            }
            // Recreate loan using stored name (loan type) and amount. We cannot perfectly reconstruct interest type and term from action only;
            // but when recording we included loanID and extra remaining. For simplicity, treat redo apply as adding a loan with principal = amount and simple interest snapshotless.
            Loan* ln = createLoanRecord(action.amount, 0.0, LOAN_SIMPLE, 1, action.name); // fallback minimal
            ln->loanID = action.loanID;
            ln->remaining = action.extra;
            // add to account
            ln->next = acc1->loans;
            acc1->loans = ln;
            // credit principal back
            acc1->balance += action.amount;
            addTransaction(acc1, "Redo Loan Disbursed", action.amount, -1);
            inverse = action;
            pushAction(&undoTop, inverse);
            printf("Redo loan apply attempted (best-effort).\n");
            break;
        }

        case ACT_LOAN_PAYMENT: {
            acc1 = searchAccount(*rootPtr, action.accNo1);
            if (!acc1) {
                printf("Account not found for redo loan payment.\n");
                break;
            }
            Loan* ln = findLoan(acc1, action.loanID);
            if (!ln) {
                printf("Loan not found for redo payment.\n");
                break;
            }
            // redo payment: subtract amount and reduce remaining
            if (acc1->balance < action.amount) {
                printf("Cannot redo loan payment, insufficient balance.\n");
                break;
            }
            acc1->balance -= action.amount;
            ln->remaining -= action.amount;
            if (ln->remaining <= 0.0) {
                ln->remaining = 0.0;
                ln->status = LOAN_CLOSED;
            }
            addTransaction(acc1, "Redo Loan Payment", action.amount, -1);
            inverse = action;
            pushAction(&undoTop, inverse);
            printf("Redo loan payment successful.\n");
            break;
        }

        case ACT_LOAN_CLOSE: {
            acc1 = searchAccount(*rootPtr, action.accNo1);
            if (!acc1) {
                printf("Account not found for redo loan close.\n");
                break;
            }
            Loan* ln = findLoan(acc1, action.loanID);
            if (!ln) { printf("Loan not found for redo close.\n"); break; }
            ln->status = LOAN_CLOSED;
            inverse = action;
            pushAction(&undoTop, inverse);
            printf("Redo loan close successful.\n");
            break;
        }

        default:
            printf("Unknown action type for redo.\n");
            break;
    }
}

/* -------- Queue functions -------- */

void enqueueCustomer(int accNo) {
    QueueNode* node = (QueueNode*)malloc(sizeof(QueueNode));
    if (!node) {
        printf("Memory allocation failed!\n");
        exit(1);
    }
    node->accNo = accNo;
    node->next = NULL;
    if (qRear == NULL) {
        qFront = qRear = node;
    } else {
        qRear->next = node;
        qRear = node;
    }
    printf("Customer with account %d added to queue.\n", accNo);
}

void serveCustomer() {
    if (qFront == NULL) {
        printf("No customers in queue.\n");
        return;
    }
    QueueNode* temp = qFront;
    qFront = qFront->next;
    if (qFront == NULL)
        qRear = NULL;
    printf("Serving customer with account %d.\n", temp->accNo);
    free(temp);
}

/* -------- Reporting -------- */

void printAccountDetails(Account* acc) {
    if (!acc) {
        printf("Account not found.\n");
        return;
    }
    printf("\n----- Account Details -----\n");
    printf("Account No : %d\n", acc->accNo);
    printf("Name       : %s\n", acc->name);
    printf("Balance    : %d\n", acc->balance);
    printf("Transaction History:\n");

    if (!acc->history) {
        printf("  No transactions yet.\n");
    } else {
        Transaction* t = acc->history;
        while (t) {
            if (t->otherAcc != -1)
                printf("  %s | Amount: %d | Other Acc: %d\n", t->type, t->amount, t->otherAcc);
            else
                printf("  %s | Amount: %d\n", t->type, t->amount);
            t = t->next;
        }
    }
    printf("Loans:\n");
    printAllLoans(acc);
    printf("----------------------------\n");
}

void printAllAccountsInOrder(Account* root) {
    if (!root) return;
    printAllAccountsInOrder(root->left);
    printf("AccNo: %d | Name: %s | Balance: %d\n", root->accNo, root->name, root->balance);
    printAllAccountsInOrder(root->right);
}

/* -------- Utility UI -------- */

void printMainMenu() {
    printf("\n========== Banking Transaction Management System ==========\n");
    printf("1. Account Management (BST)\n");
    printf("2. Transaction Management (Linked List)\n");
    printf("3. Undo / Redo (Stacks)\n");
    printf("4. Customer Service (Queue)\n");
    printf("5. Transaction Tracking & Reporting\n");
    printf("6. Loan Services\n");
    printf("7. Exit\n");
    printf("Enter choice: ");
}

/* ===================== MAIN ===================== */

int main() {
    Account* root = NULL;
    int mainChoice;

    while (1) {
        printMainMenu();
        if (scanf("%d", &mainChoice) != 1) {
            printf("Invalid input.\n");
            flushInput();
            continue;
        }

        if (mainChoice == 1) {
            int ch;
            while (1) {
                printf("\n--- Account Management ---\n");
                printf("1. Create New Account\n");
                printf("2. Search Account\n");
                printf("3. Delete Account\n");
                printf("4. Update Account\n");
                printf("5. Back to Main Menu\n");
                printf("Enter choice: ");
                if (scanf("%d", &ch) != 1) {
                    printf("Invalid input.\n");
                    flushInput();
                    continue;
                }
                if (ch == 1) {
                    createNewAccount(&root);
                    clearStack(&redoTop);
                } else if (ch == 2) {
                    int accNo;
                    printf("Enter account number to search: ");
                    if (scanf("%d", &accNo) != 1) {
                        printf("Invalid input.\n");
                        flushInput();
                        continue;
                    }
                    Account* acc = searchAccount(root, accNo);
                    if (acc)
                        printAccountDetails(acc);
                    else
                        printf("Account not found.\n");
                } else if (ch == 3) {
                    int accNo;
                    printf("Enter account number to delete: ");
                    if (scanf("%d", &accNo) != 1) {
                        printf("Invalid input.\n");
                        flushInput();
                        continue;
                    }
                    Account snapshot;
                    snapshot.history = NULL;
                    snapshot.loans = NULL;
                    Account* found = searchAccount(root, accNo);
                    if (!found) {
                        printf("Account not found.\n");
                        continue;
                    }
                    snapshot.accNo = found->accNo;
                    strncpy(snapshot.name, found->name, NAME_SIZE - 1);
                    snapshot.name[NAME_SIZE - 1] = '\0';
                    snapshot.balance = found->balance;
                    snapshot.history = found->history;
                    snapshot.loans = found->loans;

                    root = deleteAccount(root, accNo, &snapshot);

                    recordAction(ACT_DELETE, snapshot.accNo, -1, 0, snapshot.name, -1, 0.0, snapshot.balance);
                    clearStack(&redoTop);

                    printf("Account deleted successfully.\n");
                } else if (ch == 4) {
                    updateAccount(root);
                } else if (ch == 5) {
                    break;
                } else {
                    printf("Invalid choice.\n");
                }
            }
        } else if (mainChoice == 2) {
            int ch;
            while (1) {
                printf("\n--- Transaction Management ---\n");
                printf("1. Deposit\n");
                printf("2. Withdraw\n");
                printf("3. Transfer\n");
                printf("4. Back to Main Menu\n");
                printf("Enter choice: ");
                if (scanf("%d", &ch) != 1) {
                    printf("Invalid input.\n");
                    flushInput();
                    continue;
                }
                if (ch == 1) deposit(root);
                else if (ch == 2) withdraw(root);
                else if (ch == 3) transferMoney(root);
                else if (ch == 4) break;
                else printf("Invalid choice.\n");
            }
        } else if (mainChoice == 3) {
            int ch;
            while (1) {
                printf("\n--- Undo / Redo ---\n");
                printf("1. Undo\n");
                printf("2. Redo\n");
                printf("3. Back to Main Menu\n");
                printf("Enter choice: ");
                if (scanf("%d", &ch) != 1) {
                    printf("Invalid input.\n");
                    flushInput();
                    continue;
                }
                if (ch == 1) {
                    undoOperation(&root);
                } else if (ch == 2) {
                    redoOperation(&root);
                } else if (ch == 3) break;
                else printf("Invalid choice.\n");
            }
        } else if (mainChoice == 4) {
            int ch;
            while (1) {
                printf("\n--- Customer Service (Queue) ---\n");
                printf("1. Add Customer to Queue\n");
                printf("2. Serve Next Customer\n");
                printf("3. Back to Main Menu\n");
                printf("Enter choice: ");
                if (scanf("%d", &ch) != 1) {
                    printf("Invalid input.\n");
                    flushInput();
                    continue;
                }
                if (ch == 1) {
                    int accNo;
                    printf("Enter account number: ");
                    if (scanf("%d", &accNo) != 1) {
                        printf("Invalid input.\n");
                        flushInput();
                        continue;
                    }
                    enqueueCustomer(accNo);
                } else if (ch == 2) {
                    serveCustomer();
                } else if (ch == 3) break;
                else printf("Invalid choice.\n");
            }
        } else if (mainChoice == 5) {
            int ch;
            while (1) {
                printf("\n--- Transaction Tracking & Reporting ---\n");
                printf("1. Show Account Details (with history)\n");
                printf("2. Display All Accounts (In-order BST)\n");
                printf("3. Back to Main Menu\n");
                printf("Enter choice: ");
                if (scanf("%d", &ch) != 1) {
                    printf("Invalid input.\n");
                    flushInput();
                    continue;
                }
                if (ch == 1) {
                    int accNo;
                    printf("Enter account number: ");
                    if (scanf("%d", &accNo) != 1) {
                        printf("Invalid input.\n");
                        flushInput();
                        continue;
                    }
                    Account* acc = searchAccount(root, accNo);
                    printAccountDetails(acc);
                } else if (ch == 2) {
                    printf("\nAll accounts (BST in-order traversal):\n");
                    printAllAccountsInOrder(root);
                } else if (ch == 3) break;
                else printf("Invalid choice.\n");
            }
        } else if (mainChoice == 6) {
            int ch;
            while (1) {
                printf("\n--- Loan Services ---\n");
                printf("1. Apply for Loan\n");
                printf("2. Pay Loan\n");
                printf("3. Check Loan Status (by Acc No)\n");
                printf("4. Back to Main Menu\n");
                printf("Enter choice: ");
                if (scanf("%d", &ch) != 1) {
                    printf("Invalid input.\n");
                    flushInput();
                    continue;
                }
                if (ch == 1) applyLoan(root);
                else if (ch == 2) payLoan(root);
                else if (ch == 3) {
                    int accNo;
                    printf("Enter account number: ");
                    if (scanf("%d", &accNo) != 1) {
                        printf("Invalid input.\n");
                        flushInput();
                        continue;
                    }
                    Account* acc = searchAccount(root, accNo);
                    if (!acc) { printf("Account not found.\n"); continue; }
                    printAllLoans(acc);
                }
                else if (ch == 4) break;
                else printf("Invalid choice.\n");
            }
        } else if (mainChoice == 7) {
            printf("Exiting...\n");
            break;
        } else {
            printf("Invalid main menu choice.\n");
        }
    }

    return 0;
}
