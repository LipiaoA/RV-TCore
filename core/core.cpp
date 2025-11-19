#include "core.h"
#include "branch_predictor.h"
#include "rob.h"
#include "commit_unit.h"
#include "decode_unit.h"
#include "dispatch_unit.h"
#include "execute_unit.h"
#include "fetch_unit.h"
#include "issue_unit.h"
#include "rename_unit.h"
#include "branch_predictor.h"
#include "log.h"

void Core::build() {
    auto &log = LogSystem::instance();
    LOG_LEVEL1("CORE", "core building ...");

    std::string dev_name = "unknow";
    IRob *rob = new ROB("rob");
    rob->build();
    IFetch *fetch = dynamic_cast<IFetch *>(new FetchUnit(dev_name));

    ICommit *commit = dynamic_cast<ICommit *>(new CommitUnit(dev_name));
    IDecode *decode = dynamic_cast<IDecode *>(new DecodeUnit(dev_name));
    IDispatch *dispatch = dynamic_cast<IDispatch *>(new DispatchUnit(dev_name));
    IExecute *exec = dynamic_cast<IExecute *>(new ExecuteUnit(dev_name));
    IIssue *issue = dynamic_cast<IIssue *>(new IssueUnit(dev_name));
    IRename *rename = dynamic_cast<IRename *>(new RenameUnit(dev_name));
    IPredictor *predictor = dynamic_cast<IPredictor *>(new BranchPredictor(dev_name));

    add_module(fetch);
    add_module(predictor);
    add_module(decode);
    add_module(commit);
    add_module(issue);
    add_module(exec);
    add_module(rob);
    add_module(rename);
    add_module(dispatch);

    return;
}

