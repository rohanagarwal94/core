/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4; fill-column: 100 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <algorithm>
#include <functional>

#include "clang/AST/Comment.h"

#include "plugin.hxx"

// Remove dynamic exception specifications.  See the mail thread starting at
// <https://lists.freedesktop.org/archives/libreoffice/2017-January/076665.html>
// "Dynamic Exception Specifications" for details.

namespace {

bool isOverriding(FunctionDecl const * decl) {
    return decl->hasAttr<OverrideAttr>();
}

class DynExcSpec:
    public RecursiveASTVisitor<DynExcSpec>, public loplugin::RewritePlugin
{
public:
    explicit DynExcSpec(InstantiationData const & data): RewritePlugin(data) {}

    void run() override {
        // See the mail thread mentioned above for why !LIBO_INTERNAL_ONLY is
        // excluded for now:
        if (!compiler.getPreprocessor().getIdentifierInfo("LIBO_INTERNAL_ONLY")
            ->hasMacroDefinition())
        {
            return;
        }
        if (compiler.getLangOpts().CPlusPlus) {
            TraverseDecl(compiler.getASTContext().getTranslationUnitDecl());
        }
    }

    bool VisitFunctionDecl(FunctionDecl const * decl) {
        if (ignoreLocation(decl)) {
            return true;
        }
        auto proto = dyn_cast<FunctionProtoType>(decl->getType());
        if (proto == nullptr || proto->getExceptionSpecType() != EST_Dynamic) {
            return true;
        }
        if (decl->isCanonicalDecl() && !isOverriding(decl)
            && !anyRedeclHasThrowsDocumentation(decl))
        {
            report(
                DiagnosticsEngine::Warning,
                ("function declaration has dynamic exception specification but"
                 " no corresponding documentation comment"),
                decl->getLocation())
                << decl->getSourceRange();
            return true;
        }
#if 0 // will be enabled later
        bool dtor = isa<CXXDestructorDecl>(decl);
        auto source = decl->getExceptionSpecSourceRange();
        if (rewriter != nullptr && source.isValid()) {
            if (dtor) {
                if (replaceText(source, "noexcept(false)")) {
                    return true;
                }
            } else {
                auto beg = source.getBegin();
                if (beg.isFileID()) {
                    for (;;) {
                        auto prev = Lexer::GetBeginningOfToken(
                            beg.getLocWithOffset(-1),
                            compiler.getSourceManager(),
                            compiler.getLangOpts());
                        auto n = Lexer::MeasureTokenLength(
                            prev, compiler.getSourceManager(),
                            compiler.getLangOpts());
                        auto s = StringRef(
                            compiler.getSourceManager().getCharacterData(prev),
                            n);
                        while (s.startswith("\\\n")) {
                            s = s.drop_front(2);
                            while (!s.empty()
                                   && (s.front() == ' ' || s.front() == '\t'
                                       || s.front() == '\n' || s.front() == '\v'
                                       || s.front() == '\f'))
                            {
                                s = s.drop_front(1);
                            }
                        }
                        if (!s.empty() && s != "\\") {
                            break;
                        }
                        beg = prev;
                    }
                }
                if (removeText(SourceRange(beg, source.getEnd()))) {
                    return true;
                }
            }
        }
        report(
            DiagnosticsEngine::Warning,
            (dtor
             ? "replace dynamic exception specification with 'noexcept(false)'"
             : "remove dynamic exception specification"),
            source.isValid() ? source.getBegin() : decl->getLocation())
            << (source.isValid() ? source : decl->getSourceRange());
#endif
        return true;
    }

private:
    bool hasThrowsDocumentation(FunctionDecl const * decl) {
        if (auto cmt = compiler.getASTContext().getCommentForDecl(
            decl, &compiler.getPreprocessor()))
        {
            for (auto i = cmt->child_begin(); i != cmt->child_end(); ++i) {
                if (auto bcc = dyn_cast<comments::BlockCommandComment>(*i)) {
                    if (compiler.getASTContext().getCommentCommandTraits()
                        .getCommandInfo(bcc->getCommandID())->IsThrowsCommand)
                    {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    bool anyRedeclHasThrowsDocumentation(FunctionDecl const * decl) {
        return std::any_of(
            decl->redecls_begin(), decl->redecls_end(),
            [this](FunctionDecl * d) { return hasThrowsDocumentation(d); });
            // std::bind(
            //     &DynExcSpec::hasThrowsDocumentation, this,
            //     std::placeholders::_1));
    }
};

loplugin::Plugin::Registration<DynExcSpec> X("dynexcspec", true);

}

/* vim:set shiftwidth=4 softtabstop=4 expandtab cinoptions=b1,g0,N-s cinkeys+=0=break: */
