/*
    Pirate Bulletin Board System
    Copyright (C) 1990, Edward Luke, lush@Athena.EE.MsState.EDU
    Eagles Bulletin Board System
    Copyright (C) 1992, Raymond Rocker, rocker@rock.b11.ingr.com
                        Guy Vega, gtvega@seabass.st.usm.edu
                        Dominic Tynes, dbtynes@seabass.st.usm.edu
    Firebird Bulletin Board System
    Copyright (C) 1996, Hsien-Tsung Chang, Smallpig.bbs@bbs.cs.ccu.edu.tw
                        Peng Piaw Foong, ppfoong@csie.ncu.edu.tw
    
    Copyright (C) 1999, KCN,Zhou Lin, kcn@cic.tsinghua.edu.cn

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 1, or (at your option)
    any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
*/
#include <sys/ipc.h>
#include <sys/msg.h>
#include "bbs.h"
#include "bbstelnet.h"
#include "edit.h"

struct postheader header;
int continue_flag;
int readpost;
int digestmode;
struct userec *currentuser = NULL;
//usernum 是以 1 开始计数的，passwd中的位置
int usernum = 0;
int local_article;
char currboard[STRLEN];
time_t currboardstarttime = 0;
char IScurrBM = 0;
char ISdelrq = 0;
int selboard = 0;
int isattached = 0;
int inprison = 0;
int inBBSNET = 0;
struct mmapfile mf_badwords;
struct mmapfile mf_sbadwords;
struct mmapfile mf_pbadwords;

static float myexp(float x);
static int isowner(struct userec *user, struct fileheader *fileinfo);
static int UndeleteArticle(int ent, struct fileheader *fileinfo, char *direct);
static void cpyfilename(struct fileheader *fhdr);
static int read_post(int ent, struct fileheader *fileinfo, char *direct);
static int do_select(int ent, struct fileheader *fileinfo, char *direct);
static int dele_digest(int filetime, char *direc);
static int garbage_line(char *str);
static void getcross(char *filepath, int mode, int hide_bname,
		     struct fileheader *old_fh);
static int post_cross(char *bname, int mode, int islocal, int hascheck,
		      int dangerous, int hide_bname, struct fileheader *old_fh);
static int post_article(struct fileheader *sfh);
static int dofilter(char *title, char *fn, int mode);
static int edit_title(int ent, struct fileheader *fileinfo, char *direct);
static int markspec_post(int ent, struct fileheader *fileinfo, char *direct);
static int import_spec(void);
static int moveintobacknumber(int ent, struct fileheader *fileinfo,
			      char *direct);
static int del_post_backup(int ent, struct fileheader *fileinfo, char *direct);
static int sequent_messages(struct fileheader *fptr);
static int sequential_read(int ent, struct fileheader *fileinfo, char *direct);
static int sequential_read2(int ent);
static void quickviewpost(int ent, struct fileheader *fileinfo, char *direct);
static int change_t_lines(void);
static int post_saved(void);
static int show_b_secnote(void);
static int show_b_note(void);
static int show_file_info(int ent, struct fileheader *fileinfo, char *direct);
static int do_t_query(void);
static int into_backnumber(void);
static int into_my_Personal(void);
static int select_Personal(void);
static void notepad(void);
static int Origin2(char text[256]);
static int deny_me_global(void);
static int do_thread(char *board);
static int skipattach(char *buf, int size, FILE * fp);
static int clear_new_flag(int ent, struct fileheader *fileinfo, char *direct);
static int b_notes_edit();
static int b_notes_passwd();
static int catnotepad(FILE * fp, char *fname);
static int change_content_title(char *fname, char *title);

/* 进行一个问答, 答错返回-1, 否则返回 0*/
int
show_cake()
{
	unsigned int r;
	char buf[256];
	char input[3];
	getrandomint(&r);
	r = r % 9 + 1;
	sprintf(buf, "etc/fonts/txt/%d", r);
	clear();
	prints("\033[1;28H\033[35mA PIECE OF CAKE\033[2;29H小 菜 一 碟"
	       "\033[0m\n\n\n");
	ansimore2(buf, NA, 2, 24);
	sprintf(buf, " 请输入上面的数字: ");
	getdata(20, 1, buf, input, 3, DOECHO, YEA);
	if (atoi(input) != r) {
		prints("\033[22;1H...好象不大对吧，您还有一次机会...");
		getdata(20, 1, buf, input, 3, DOECHO, YEA);
		if (atoi(input) != r) {
			prints("啊? 竟然还不对? 真的是你么, 我好伤心...");
			pressreturn();
			return -1;
		}
	}
	pressreturn();
	return 0;

}

/* 在post_article中使用, 用来限制灌水 */
#define DELAYFACTOR1 20.
#define DELAYFACTOR2 100
#define DELAYFACTOR3 25
#define DELAYTIME 10

/* *INDENT-OFF* */
static float
myexp(float x)
{
	if (x < -4)
		return 0;
	return 1 + x * (1 + x / 2 * (1 + x / 3 * (1 + x / 4 * (1 + x / 5 * (1 + x / 6 * 
	       (1 + x / 7 * (1 + x / 8 * (1 + x / 9 * (1 + x / 10 * (1 + x / 11 * (1 + x
	       / 12 * (1 + x / 13 * (1 + x / 14 * (1 + x / 15 / 1.4))))))))))))));
}
/* *INDENT-ON* */

void
do_delay(int i)
{
	static time_t postlasttime = 0, ppt;
	static float postactivelevel = 0, ppa;
	time_t timenow;

	float ee;
	if (i == -1) {
		postactivelevel = ppa;
		postlasttime = ppt;
		return;
	}
	ppa = postactivelevel;
	ppt = postlasttime;
	timenow = time(NULL);
	ee = myexp(-(timenow - postlasttime) / DELAYFACTOR1 -
		   0.5 * (((timenow - postlasttime) < 4)
			  ? (timenow - postlasttime - 4) : 0));
	if (postactivelevel * ee > DELAYFACTOR2) {
		if (show_cake()) {
			Q_Goodbye();
		}
		ppa /= 2;
		postactivelevel /= 2;
	}
	postactivelevel = postactivelevel * ee + DELAYFACTOR3 * i;
	postlasttime = timenow;
}

/*------end by ylsdd-----*/
/* start by gluon for no reply */
/* Added by deardragon 1999.11.21 增加不可 RE 属性 */
int
underline_post(ent, fileinfo, direct)
int ent;
struct fileheader *fileinfo;
char *direct;
{
	if (!strcmp(MY_BBS_ID, "TTTAN"))
		return DONOTHING;
	if (!IScurrBM && !isowner(currentuser, fileinfo)) {
		return DONOTHING;
	}
	change_dir(direct, fileinfo, (void *) DIR_do_underline, ent,
		   digestmode, 0, NULL);
	return PARTUPDATE;
}

static int
watch_board(bname)
char *bname;
{
	int i;

	if ((i = getbnum(bname)) == 0)
		return 0;
	return (bbsinfo.bcache[i - 1].header.flag2 & WATCH_FLAG);
}

static void
log_article(struct fileheader *fh, char *oldpath, char *board)
{
	int count;
	int t;
	char filepath[STRLEN], newfname[STRLEN];
	char buf[256];
	struct fileheader postfile;

	if (!strcmp(fh->owner, "deliver"))
		return;
	if (watch_board(board))
		return;
	snprintf(buf, sizeof (buf), "..%s", oldpath + 6);

	memcpy(&postfile, fh, sizeof (struct fileheader));
	if (strncmp(fh->title, "Re: ", 4))
		snprintf(postfile.title, sizeof (postfile.title), "%s %s",
			 board, fh->title);
	else
		snprintf(postfile.title, sizeof (postfile.title), "Re: %s %s",
			 board, fh->title + 4);
	t = postfile.filetime;
	count = 0;
	while (1) {
		sprintf(newfname, "M.%d.A", t);
		setbfile(filepath, "allarticle", newfname);
		if (symlink(buf, filepath) == 0) {
			postfile.filetime = t;
			count = -1;
			break;
		}
		if (errno != EEXIST)
			break;
		t += 10;
		if (count++ > MAX_POSTRETRY)
			break;
	}
	if (count != -1)
		return;
	setbdir(filepath, "allarticle", 0);
	append_record(filepath, &postfile, sizeof (struct fileheader));
	updatelastpost("allarticle");
}

int
allcanre_post(ent, fileinfo, direct)
int ent;
struct fileheader *fileinfo;
char *direct;
{
	if (!USERPERM(currentuser, PERM_SYSOP))
		return DONOTHING;
	change_dir(direct, fileinfo, (void *) DIR_do_allcanre, ent,
		   digestmode, 0, NULL);
	return PARTUPDATE;
}

/* Added End. */

char ReadPost[STRLEN] = "";
char ReplyPost[STRLEN] = "";
int readingthread;

char genbuf[1024];
char quote_title[120], quote_board[120];
char quote_file[120], quote_user[120];

static int
isowner(user, fileinfo)
struct userec *user;
struct fileheader *fileinfo;
{
	if (strcmp(fileinfo->owner, user->userid))
		return 0;
	if (fileinfo->filetime < user->firstlogin)
		return 0;
	if (!strcmp(user->userid, "guest"))
		return 0;
	return 1;
}

/*Add by SmallPig*/
void
setqtitle(stitle)
char *stitle;
{
	if (strncmp(stitle, "Re: ", 4) != 0 && strncmp(stitle, "RE: ", 4) != 0) {
		snprintf(ReplyPost, 55, "Re: %s", stitle);
		strcpy(ReadPost, stitle);
	} else {
		strcpy(ReplyPost, stitle);
		strcpy(ReadPost, ReplyPost + 4);
	}
}

int
chk_currBM(bh, isbig)
struct boardheader *bh;
int isbig;
{
	if (USERPERM(currentuser, PERM_BLEVELS))
		return YEA;

	if (USERPERM(currentuser, PERM_SPECIAL4)
	    && issecm(bh->sec1, currentuser->userid))
		return YEA;

	return (USERPERM(currentuser, PERM_BOARDS)
		&& chk_BM(currentuser, bh, isbig));
}

void
setquotefile(filepath)
char filepath[];
{
	strcpy(quote_file, filepath);
}

char *
setuserfile(buf, filename)
char *buf, *filename;
{
	return sethomefile(buf, currentuser->userid, filename);
}

char *
setbpath(buf, boardname)
char *buf, *boardname;
{
	strcpy(buf, "boards/");
	strcat(buf, boardname);
	return buf;
}

char *
setbfile(buf, boardname, filename)
char *buf, *boardname, *filename;
{
	sprintf(buf, "boards/%s/%s", boardname, filename);
	return buf;
}

int
deny_me(char *bname)
{
	char buf[STRLEN];
	int deny1, deny2;
	setbfile(buf, bname, "deny_users");
	deny1 = seek_in_file(buf, currentuser->userid);
	setbfile(buf, bname, "deny_anony");
	deny2 = seek_in_file(buf, currentuser->userid);
	return (deny1 || deny2);
}

static int
deny_me_global()
{
	return seek_in_file("deny_users", currentuser->userid);
}

/*Add by SmallPig*/
void
shownotepad()
{
	modify_user_mode(NOTEPAD);
	if (file_exist("etc/notepad"))
		ansimore("etc/notepad", YEA);
	clear();
	return;
}

int
g_board_names(fhdrp, param)
struct boardmem *fhdrp;
void *param;
{
	if (fhdrp->header.filename[0]
	    && hasreadperm(&(fhdrp->header))) {
		AddNameList(fhdrp->header.filename);
	}
	return 0;
}

void
make_blist()
{
	CreateNameList();
	apply_boards(g_board_names, NULL);
}

int
g_board_names_full(fhdrp, param)
struct boardmem *fhdrp;
void *param;
{
	if (fhdrp->header.filename[0])
		AddNameList(fhdrp->header.filename);
	return 0;
}

void
make_blist_full()
{
	CreateNameList();
	apply_boards(g_board_names_full, NULL);
}

int
junkboard()
{
	return seek_in_file("etc/junkboards", currboard)
	    || club_board(currboard, 0);
}

void
Select()
{
	modify_user_mode(SELECT);
	do_select(0, NULL, NULL);
	Read();
}

int
postfile(filename, nboard, posttitle, mode)
char *filename, *nboard, *posttitle;
int mode;
{
	int retv;
	int save_in_mail;
	save_in_mail = in_mail;
	in_mail = NA;
	strcpy(quote_board, nboard);
	strcpy(quote_file, filename);
	strcpy(quote_title, posttitle);
	retv = post_cross(nboard, mode, 1, 0, 0, 0, NULL);
	in_mail = save_in_mail;
	return retv;
}

int
get_a_boardname(bname, prompt)
char *bname, *prompt;
{
	struct boardheader bh;

	make_blist();
	namecomplete(prompt, bname);
	FreeNameList();
	if (*bname == '\0') {
		return 0;
	}
	if (new_search_record
	    (BOARDS, &bh, sizeof (struct boardheader), (void *) cmpbnames,
	     bname) <= 0) {
		move(1, 0);
		prints("错误的讨论区名称\n");
		pressreturn();
		move(1, 0);
		return 0;
	}
	return 1;
}

/* undelete 一篇文章 Leeward 98.05.18 */
/* modified by ylsdd */
static int
UndeleteArticle(ent, fileinfo, direct)
int ent;
struct fileheader *fileinfo;
char *direct;
{
	char *p, buf[1024];
	char UTitle[128];
	char filepath[STRLEN];
	struct fileheader UFile;
	int i;
	FILE *fp;

	if (digestmode != 4 && digestmode != 5)
		return DONOTHING;
	if (!IScurrBM)
		return DONOTHING;
	sprintf(filepath, "boards/%s/%s", currboard, fh2fname(fileinfo));
	if (!file_isfile(filepath)) {
		clear();
		move(2, 0);
		prints("该文章不存在，已被恢复, 删除或列表出错");
		pressreturn();
		return FULLUPDATE;
	}
	fp = fopen(filepath, "r");
	if (!fp)
		return DONOTHING;

	strcpy(UTitle, fileinfo->title);
	if ((p = strrchr(UTitle, '-'))) {	/* create default article title */
		*p = 0;
		for (i = strlen(UTitle) - 1; i >= 1; i--) {
			if (UTitle[i] != ' ')
				break;
			else
				UTitle[i] = 0;
		}
	}

	i = 0;
	while (!feof(fp) && i < 2) {
		fgets(buf, 1024, fp);
		if (feof(fp))
			break;
		if (strstr(buf, "发信人: ") && strstr(buf, "), 信区: ")) {
			i++;
		} else if (strstr(buf, "标  题: ")) {
			i++;
			strcpy(UTitle, buf + 8);
			if ((p = strchr(UTitle, '\n')))
				*p = 0;
		}
	}
	fclose(fp);

	UFile = *fileinfo;
	strsncpy(UFile.title, UTitle, sizeof (UFile.title));
	UFile.deltime = 0;
	UFile.accessed &= ~FH_DEL;

	if ((fileinfo->accessed & FH_ISDIGEST))
		sprintf(buf, "boards/%s/.DIGEST", currboard);
	else
		sprintf(buf, "boards/%s/.DIR", currboard);
	if (1) {
		char newfilepath[STRLEN], newfname[STRLEN];
		int count, now;
		now = time(NULL);
		count = 0;
		while (1) {
			sprintf(newfname, "%c.%d.A",
				(fileinfo->accessed & FH_ISDIGEST) ? 'G' : 'M',
				(int) now);
			setbfile(newfilepath, currboard, newfname);
			if (link(filepath, newfilepath) == 0) {
				unlink(filepath);
				UFile.filetime = now;
				break;
			}
			now++;
			if (count++ > MAX_POSTRETRY)
				break;
		}
	}
	fh_find_thread(&UFile, currboard);
	append_record(buf, &UFile, sizeof (struct fileheader));
	updatelastpost(currboard);
	fileinfo->filetime = 0;
	substitute_record(direct, fileinfo, sizeof (struct fileheader), ent);
	tracelog("%s undel %s %s %s", currentuser->userid, currboard,
		 UFile.owner, UFile.title);
	clear();
	move(2, 0);
	prints("'%s' 已恢复到版面 \n", UFile.title);
	pressreturn();

	return FULLUPDATE;
}

/* Add by SmallPig */
int
do_cross(ent, fileinfo, direct)
int ent;
struct fileheader *fileinfo;
char *direct;
{
	char bname[STRLEN];
	char ispost[10];
	int ddigestmode;
	int islocal;
	int hide1, hide2;
	int dangerous;
	char tmpfn[STRLEN];

	if (!USERPERM(currentuser, PERM_POST))
		return DONOTHING;

	if (inprison) {
		move(0, 0);
		clear();
		prints("安心坐牢,不要胡闹! :)");
		pressanykey();
		return FULLUPDATE;
	}

	if (currentuser->dieday) {
		move(0, 0);
		clear();
		prints
		    ("不颡镥族稹弩篑啊,豚犭卷囡甑点喔.\n(这是鬼话,你不懂的,反正就是不让转贴就是啦!!!)");
		pressanykey();
		return FULLUPDATE;
	}

	if (uinfo.mode == RMAIL || in_mail)
		setmailfile(genbuf, currentuser->userid, fh2fname(fileinfo));
	else if (uinfo.mode == BACKNUMBER)
		directfile(genbuf, direct, fh2fname(fileinfo));
	else
		sprintf(genbuf, "boards/%s/%s", currboard, fh2fname(fileinfo));;
	strsncpy(quote_file, genbuf, sizeof (quote_file));
	strsncpy(quote_title, fileinfo->title, sizeof (quote_title));

	clear();
	prints
	    ("\033[1m请注意：本站站规规定：内容相同或类似的文章严禁在\033[31m5(不含)\033[37m个以上讨论区重复张贴。\n");
	prints
	    ("\033[1m转贴超过5个讨论区者除所贴文章会被全部删除之外，还将被剥夺全站发表文章的权利。\n");
	prints
	    ("\033[1m             请大家共同维护 BBS 的环境，节省系统资源。谢谢合作。\n\033[0m");
	move(4, 0);
	if (!get_a_boardname(bname, "请输入要转贴的讨论区名称: ")) {
		return FULLUPDATE;
	}
	hide1 = hideboard(currboard);
	hide2 = hideboard(bname);
	if ((uinfo.mode != RMAIL) && (hide1 && !hide2))
		return FULLUPDATE;
	sprintf(tmpfn, MY_BBS_HOME "/bbstmpfs/tmp/filter.%s.%d",
		currentuser->userid, getpid());
	copyfile(quote_file, tmpfn);
	filter_attach(tmpfn);
	dangerous = dofilter(quote_title, tmpfn, political_board(bname, 0));
	unlink(tmpfn);
	if (!hide2 && ((uinfo.mode == RMAIL) || (uinfo.mode == BACKNUMBER))
	    && dangerous == -1) {
#ifdef POST_WARNING
		char mtitle[256];
		snprintf(mtitle, sizeof (mtitle), "[转载报警] %s %.60s",
			 bname, quote_title);
		system_mail_file(quote_file, "delete", mtitle,
				 currentuser->userid);
		updatelastpost("deleterequest");
#endif
	}
	if (is1984_board(bname))
		return FULLUPDATE;
	move(5, 0);
	clrtoeol();
	prints("转载 ' %s ' 到 %s 版 ", quote_title, bname);
	move(6, 0);
	if (innd_board(bname)) {
		getdata(7, 0, "(S)发表 (L)不转信 (A)取消? [A]: ", ispost,
			9, DOECHO, YEA);
		islocal = 0;
		if (ispost[0] == 'l' || ispost[0] == 'L') {
			islocal = 1;
			ispost[0] = 's';
		}
	} else if (njuinn_board(bname)) {
		getdata(7, 0, "(L)发表 (S)转信 (A)取消? [A]: ", ispost,
			9, DOECHO, YEA);
		islocal = 1;
		if (ispost[0] == 's' || ispost[0] == 'S') {
			islocal = 0;
		}
		if (ispost[0] == 'l' || ispost[0] == 'L')
			ispost[0] = 's';
	} else {
		getdata(7, 0, "(L)发表 (A)取消? [A]: ", ispost, 9, DOECHO, YEA);
		if (ispost[0] == 'l' || ispost[0] == 'L')
			ispost[0] = 's';
		islocal = 1;
	}
	if (ispost[0] == 's' || ispost[0] == 'S') {
		if (deny_me(bname) && !USERPERM(currentuser, PERM_SYSOP)) {
			move(8, 0);
			clrtobot();
			prints
			    ("\n\n                 很抱歉，您已经被版主停止了发贴的权利。");
			pressreturn();
			clear();
			return FULLUPDATE;
		}
		if (deny_me_global() && !USERPERM(currentuser, PERM_SYSOP)) {
			move(8, 0);
			clrtobot();
			prints
			    ("\n\n                 很抱歉，您已经被站务停止了全站的发贴权利。");
			pressreturn();
			clear();
			return FULLUPDATE;
		}
		if (club_board(bname, 0)) {
			if (!clubtest(bname)
			    && !USERPERM(currentuser, PERM_SYSOP)) {
				move(8, 0);
				clrtobot();
				prints
				    ("\n\n               %s为俱乐部版面，请向版务申请发文权限。",
				     bname);
				pressreturn();
				clear();
				return FULLUPDATE;
			}
		}
		strcpy(quote_board, currboard);
		ddigestmode = digestmode;
		digestmode = 0;

		if (post_cross(bname, 0, islocal, 1, dangerous, hide1, fileinfo)
		    == -1) {
			move(8, 0);
			prints("Failed!");
			pressreturn();
			digestmode = ddigestmode;
			return FULLUPDATE;
		}
		digestmode = ddigestmode;
		move(8, 0);
		prints("' %s ' 已转贴到 %s 版 \n", quote_title, bname);
	} else {
		move(8, 0);
		prints("取消");
	}
	pressreturn();
	return FULLUPDATE;
}

int
do_collect(ent, fileinfo, direct)	//Fishingsnow 2005.03.26
int ent;
struct fileheader *fileinfo;
char *direct;
{
	struct boardheader bh;
	struct fileheader postfile;
	char collectboard[STRLEN], fname[STRLEN], buf[256];
	int pos, ddigestmode, count, t;

	if (inprison) {
		clear();
		move(4, 6);
		prints("牢狱文学没有稿费，还是安心改造吧！");
		pressanykey();
		return FULLUPDATE;
	}
	if (currentuser->dieday) {
		clear();
		move(4, 6);
		prints("遗作发表请先联络铁笔判官！");
		pressanykey();
		return FULLUPDATE;
	}
	if (hideboard(currboard)) {
		clear();
		move(4, 6);
		prints("快来人啊！%s 要泄露这里的秘密，快抓回去灭口！",
		       currentuser->userid);
		pressanykey();
		return FULLUPDATE;
	}

	pos =
	    new_search_record(BOARDS, &bh, sizeof (struct boardheader),
			      (void *) cmpbnames, currboard);
	if (pos <= 0)
		return DONOTHING;
	if (!strcmp("collection", currboard + 1))
		strcpy(collectboard, "collection");
	else
		sprintf(collectboard, "%scollection", bh.sec1);
	pos =
	    new_search_record(BOARDS, &bh, sizeof (struct boardheader),
			      (void *) cmpbnames, collectboard);
	if (pos <= 0) {
		clear();
		move(4, 6);
		prints
		    ("当前讨论区所在的分区没有文摘版面，不能进行收录文摘操作。");
		pressanykey();
		return FULLUPDATE;
	}
	if (deny_me(collectboard) && !USERPERM(currentuser, PERM_SYSOP)) {
		clear();
		move(4, 6);
		prints("很抱歉，您已经被文摘版版务停止了添加文摘的权利。");
		pressanykey();
		return FULLUPDATE;
	}
	if (deny_me_global() && !USERPERM(currentuser, PERM_SYSOP)) {
		clear();
		move(4, 6);
		prints("很抱歉，您已经被站务停止了在全站的POST权利。");
		pressanykey();
		return FULLUPDATE;
	}

	if (askyn("是否将本文转载至文摘版面？", NA, YEA) == NA)
		return FULLUPDATE;
	sprintf(genbuf, "boards/%s/%s", currboard, fh2fname(fileinfo));
	ddigestmode = digestmode;
	digestmode = 0;

	memcpy(&postfile, fileinfo, sizeof (struct fileheader));
	snprintf(postfile.title, sizeof (postfile.title), "%s",
		 fileinfo->title);
	count = 0;
	snprintf(buf, sizeof (buf), "boards%s", genbuf + 6);
	t = postfile.filetime;
	while (1) {
		sprintf(fname, "M.%d.A", t);
		setbfile(genbuf, collectboard, fname);
		if (symlink(buf, genbuf) == 0) {
			postfile.filetime = t;
			break;
		}
		t ++;
		if (count++ > MAX_POSTRETRY)
			break;
	}
	setbdir(genbuf, collectboard, 0);
	append_record(genbuf, &postfile, sizeof (struct fileheader));
	updatelastpost(collectboard);

	digestmode = ddigestmode;
	clear();
	move(4, 6);
	prints(" %s 已转贴到 %s区 文摘版", postfile.title, bh.sec1);
	pressreturn();
	return FULLUPDATE;
}

int currfiletime;

int
cmpfilename(fhdr)
struct fileheader *fhdr;
{
	return (fhdr->filetime == currfiletime);
}

static void
cpyfilename(fhdr)
struct fileheader *fhdr;
{
	char buf[STRLEN];
	time_t tnow;
	struct tm *now;

	tnow = time(NULL);
	now = localtime(&tnow);

	sprintf(buf, "-%s", fhdr->owner);
	strsncpy(fhdr->owner, buf, sizeof (fhdr->owner));
	sprintf(buf, "<< 本文被 %s 于 %d/%d %d:%02d:%02d 删除 >>",
		currentuser->userid, now->tm_mon + 1, now->tm_mday,
		now->tm_hour, now->tm_min, now->tm_sec);
	strsncpy(fhdr->title, buf, sizeof (fhdr->title));
	fhdr->filetime = 0;	//表示文章被删除了
}

#ifdef ENABLE_MYSQL
static int
marked_eva(int ent, struct fileheader *fhdr, char *direct, int newmarked)
{
	int cl;
	if (hideboard(currboard))
		return 0;
	if (newmarked)
		cl = 5;
	else
		cl = 1;
	bbseva_set(utmpent, currboard, fh2fname(fhdr), "SYSOP", cl);
	return 0;
}

static int
do_evaluate(int ent, struct fileheader *fhdr, char *direct, int mode)
{
	int cl, count, ch, oldstar;
	float avg;
	if (hideboard(currboard))
		return -1;
	move(20, 0);
	clrtobot();
	if (now_t - fhdr->filetime > EVADAY * 86400) {
		if (mode)
			return -1;
		prints("\n这么老的文章就别评价了吧,去评点新文章吧 :)");
		pressreturn();
		return -1;
	}

	move(t_lines - 4, 0);
	clrtobot();
	prints("请问您对这篇文章的印象如何?\n");
	prints("0. 放弃 1. 一般 2. 好 3. 很好 4. 强烈推荐 [0]: \n");
	ch = igetkey();
	if (!isdigit(ch))
		cl = 0;
	else
		cl = ch - '0';
	if (cl == 0 && !mode)
		return 0;
	cl++;
	if (cl > 5)
		cl = 5;
	if (cl < 0)
		return 0;
	//oldstar = 0/1都认为是未评价状态
	if (bbseva_qset
	    (utmpent, currboard, fh2fname(fhdr), currentuser->userid, cl,
	     &oldstar, &count, &avg) < 0)
		return 0;
	if (oldstar != cl) {
		fhdr->staravg50 = (int) (50 * avg);
		fhdr->hasvoted = count > 255 ? 255 : count;;
		change_dir(direct, fhdr, (void *) DIR_do_evaluate, ent,
			   digestmode, 1, NULL);
	}

	if (cl == 1 || cl == oldstar) {
		if (!mode) {
			prints("您没有改变您对这篇文章的评价\n");
			pressanykey();
		}
	} else {
		if (oldstar != 1 && oldstar != 0)
			prints("您把您对这篇文章的评价从%d星级改到%d星级",
			       oldstar - 1, cl - 1);
		else
			prints("这篇文章被您评价为%d星级", cl - 1);
		pressanykey();
	}
	return 0;
}
#endif

void
board_attach_link(char *buf, int buf_len, long attachpos, char *attachname,
		  void *arg)
{
	struct fileheader *fh = (struct fileheader *) arg;
	char *ss;
	if (hideboard(currboard)) {
		ss = temp_sessionid;
	} else {
		ss = "";
	}
	snprintf(buf, buf_len,
		 "http://%s/" SMAGIC "%s/b/%d/%s/%ld/%s",
		 MY_BBS_DOMAIN, ss, getbnum(currboard) - 1,
		 fh2fname(fh), attachpos + 2, urlencode(attachname));
}

static int
read_post(ent, fileinfo, direct)
int ent;
struct fileheader *fileinfo;
char *direct;
{
	int ch;
	time_t starttime;
	int bnum;
#ifdef ENABLE_MYSQL
	int cou;
	extern int effectiveline;
	static time_t lastevaluetime = 0;
#endif
	starttime = time(NULL);
	clear();
	directfile(genbuf, direct, fh2fname(fileinfo));
	SETREAD(fileinfo, &brc);
	strsncpy(quote_file, genbuf, sizeof (quote_file));
	strsncpy(quote_board, currboard, sizeof (quote_board));
	strsncpy(quote_title, fileinfo->title, sizeof (quote_title));
	strsncpy(quote_user, fh2owner(fileinfo), sizeof (quote_user));
	isattached = 0;
	register_attach_link(board_attach_link, fileinfo);
#ifndef NOREPLY
	ch = ansimore_withzmodem(genbuf, NA, fileinfo->title);
#else
	ch = ansimore_withzmodem(genbuf, YEA, fileinfo->title);
#endif
	register_attach_link(NULL, NULL);
	if (fileinfo->accessed & FH_ALLREPLY) {
		FILE *fp;
		time_t n;
		fp = fopen("bbstmpfs/dynamic/Bvisit_log", "a");
		if (NULL != fp) {
			n = time(0);
			fprintf(fp, "telnet/ssh user %s from %s visit %s %s %s",
				currentuser->userid, fromhost, currboard,
				fh2fname(fileinfo), ctime(&n));
			fclose(fp);
		}
	}
	//fileinfo->viewtime++;
	if (!(fileinfo->accessed & FH_ATTACHED) && isattached) {
		change_dir(direct, fileinfo, (void *) DIR_do_attach, ent,
			   digestmode, 0, NULL);
	}
#ifndef NOREPLY
	move(t_lines - 1, 0);
	clrtoeol();
	bnum = getbnum(currboard);
	if (haspostperm(bnum)
	    && (time(NULL) - fileinfo->filetime < 86400 * 3)) {
		prints
		    ("\033[1;44;31m[阅读文章] \033[33m回文 R│推荐给网友 E│结束 ←│上一封↑l│下一封↓n│主题阅读 x p\033[m");
	} else {
		prints
		    ("\033[1;44;31m[阅读文章] \033[33m结束 Q,←│上一封 ↑,l│下一封 n, <Space>,<Enter>,↓│主题阅读 x p \033[m");
	}

	/* Re-Write By Excellent */

	readingthread = fileinfo->thread;
	if (strncmp(fileinfo->title, "Re: ", 4) != 0) {
		strcpy(ReplyPost, "Re: ");
		strsncpy(ReplyPost + 4, fileinfo->title,
			 sizeof (ReplyPost) - 4);
		strsncpy(ReadPost, fileinfo->title, sizeof (ReadPost));
	} else {
		strsncpy(ReplyPost, fileinfo->title, sizeof (ReplyPost));
		strsncpy(ReadPost, fileinfo->title + 4, sizeof (ReadPost));
	}

	if (!(ch == KEY_RIGHT || ch == KEY_UP || ch == KEY_PGUP
	      || ch == KEY_DOWN) && (ch <= 0 || strchr("RrEexp", ch) == NULL))
		ch = egetch();
	//if (fileinfo->accessed & FH_ISTOP)
	//      return FULLUPDATE;

#ifdef ENABLE_MYSQL
	cou = now_t - starttime;

	if (ch != 'E' && ch != 'e' && cou >= 3 && effectiveline >= 5
	    && ((now_t - lastevaluetime) >= 60 * 3)) {
		if (cou > 10)
			cou = 3;
		else if (cou > 5)
			cou = 4;
		else
			cou = 6;
		if (rand() % cou == 0) {
			if (do_evaluate(ent, fileinfo, direct, 1) >= 0)
				lastevaluetime = time(NULL);
		}
	}
#endif

	switch (ch) {
	case 'Q':
	case 'q':
	case KEY_LEFT:
		break;
	case 'j':
	case KEY_RIGHT:
		if (USERDEFINE(currentuser, DEF_THESIS)) {	/* youzi */
			sread(0, 0, ent, 0, fileinfo);
			break;
		} else {
			return READ_NEXT;
		}
	case KEY_DOWN:
	case KEY_PGDN:
	case 'n':
	case ' ':
		return READ_NEXT;
	case KEY_UP:
	case KEY_PGUP:
	case 'l':
		return READ_PREV;
	case 'Y':
	case 'R':
	case 'y':
	case 'r':
/* Added by deardragon 1999.11.21 增加不可 RE 属性 */
		if (!(fileinfo->accessed & FH_NOREPLY))
			return do_reply(fileinfo);
		else {
			move(3, 0);
			clrtobot();
			prints("\n\n    对不起, 本文被设置为不可Re!!!    ");
			pressreturn();
			clear();
		}
/* Added End. */
		break;
	case Ctrl('R'):
		post_reply(ent, fileinfo, direct);
		break;
#ifdef ENABLE_MYSQL
	case 'E':
	case 'e':
		do_evaluate(ent, fileinfo, direct, 0);
		break;
#endif
	case 'g':
		digest_post(ent, fileinfo, direct);
		break;
	case Ctrl('U'):
		sread(0, 1, ent, 1, fileinfo);
		break;
	case Ctrl('N'):
		SR_first(ent, fileinfo, direct);
		ent = sread(3, 0, ent, 0, fileinfo);
		sread(0, 1, ent, 0, fileinfo);
		break;
	case 'x':
		sread(0, 0, ent, 0, fileinfo);
		break;
	case 'p':		/*Add by SmallPig */
		sread(4, 0, ent, 0, fileinfo);
		break;
	case Ctrl('A'):	/*Add by SmallPig */
		clear();
		show_author(0, fileinfo, '\0');
		return READ_NEXT;
	case 'C':
		friend_author(0, fileinfo, '\0');
		return READ_NEXT;
	case 'S':		/* by youzi */
		if (!USERPERM(currentuser, PERM_PAGE))
			break;
		clear();
		s_msg();
		break;
	case Ctrl('D'):	/*by yuhuan for deny anonymous */
		if (!IScurrBM)
			break;
		clear();
		deny_from_article(ent, fileinfo, direct);
		break;
	case Ctrl('Y'):
		zmodem_sendfile(ent, fileinfo, direct);
		clear();
		break;
	default:
		break;
	}
#endif
	return FULLUPDATE;
}

static int
do_select(ent, fileinfo, direct)
int ent;
struct fileheader *fileinfo;
char *direct;
{
	char bname[STRLEN], bpath[STRLEN];
	struct stat st;
	int bnum;
	if (currentuser->dieday || inprison)
		return DONOTHING;
	if (digestmode == 4 || digestmode == 5)
		return DONOTHING;	//by ylsdd
	move(0, 0);
	clrtoeol();
	prints("选择一个讨论区 (英文字母大小写皆可)\n");
	prints("输入讨论区名 (按空白键自动搜寻): ");
	clrtoeol();

	make_blist();
	namecomplete((char *) NULL, bname);
	FreeNameList();
	setbpath(bpath, bname);
	if ((*bname == '\0') || (stat(bpath, &st) == -1)) {
		move(2, 0);
		clrtoeol();
		prints("不正确的讨论区.\n");
		pressreturn();
		return FULLUPDATE;
	}
	if (!(st.st_mode & S_IFDIR)) {
		move(2, 0);
		clrtoeol();
		prints("不正确的讨论区.\n");
		pressreturn();
		return FULLUPDATE;
	}
	bnum = getbnum(bname);
	if (!clubsync(bnum)) {
		move(2, 0);
		clrtoeol();
		prints("不正确的讨论区.\n");
		pressreturn();
		return FULLUPDATE;
	}
	if (bbsinfo.bcache[bnum - 1].header.flag & CLOSECLUB_FLAG) {	//封闭俱乐部
		if (bbsinfo.bcache[bnum - 1].header.flag & CLUBLEVEL_FLAG) {	//C俱乐部
			if (!USERPERM(currentuser, PERM_SYSOP)) {	//不是站站则检查俱乐部权限
				if (!clubtest(bname)) {
					move(2, 0);
					clrtoeol();
					prints("不正确的讨论区.\n");
					pressreturn();
					return FULLUPDATE;
				}
			} else if (strcmp(currentuser->userid, "SYSOP")) {	//不是SYSOP的站站检查上没上黑名单
				if (!clubtest(bname)
				    && clubtestdenysysop(bname)) {
					move(2, 0);
					clrtoeol();
					prints("不正确的讨论区.\n");
					pressreturn();
					return FULLUPDATE;
				}
			}
		} else {
			if (!clubtest(bname)) {
				move(2, 0);
				clrtoeol();
				prints("不正确的讨论区.\n");
				pressreturn();
				return FULLUPDATE;
			}
		}

	}

	if (uinfo.curboard && bbsinfo.bcache[uinfo.curboard - 1].inboard > 0) {
		bbsinfo.bcache[uinfo.curboard - 1].inboard--;
	}
	uinfo.curboard = bnum;
	update_utmp();
	if (uinfo.curboard)
		bbsinfo.bcache[uinfo.curboard - 1].inboard++;
	selboard = 1;
	brc_initial(bname, 0);
	loguseboard();
	strcpy(currboard, bname);

	move(0, 0);
	clrtoeol();
	move(1, 0);
	clrtoeol();
	digestmode = 0;
	if (direct) {
		setbdir(direct, currboard, digestmode);
		if (USERDEFINE(currentuser, DEF_FIRSTNEW)) {
			int tmp;
			if (getkeep(direct, -1, 0) == NULL) {
				tmp =
				    unread_position(direct,
						    &(bbsinfo.
						      bcache[uinfo.curboard -
							     1]));
				page = tmp - t_lines / 2;
				getkeep(direct, page > 1 ? page : 1, tmp + 1);
			}
		}
	}
	return NEWDIRECT;
}

static int
dele_digest(filetime, direc)
int filetime;
char *direc;
{
	char digest_name[STRLEN];
	char new_dir[STRLEN];
	int tmpcurrfiletime;
	struct fileheader fh;
	int pos;
	sprintf(digest_name, "G.%d.A", filetime);
	directfile(new_dir, direc, DIGEST_DIR);
	tmpcurrfiletime = currfiletime;
	currfiletime = filetime;
	pos =
	    new_search_record(new_dir, &fh, sizeof (struct fileheader),
			      (void *) cmpfilename, NULL);
	if (pos <= 0)
		return 0;
	delete_file(new_dir, sizeof (struct fileheader),
		    pos, (void *) cmpfilename);
	currfiletime = tmpcurrfiletime;
	directfile(new_dir, direc, digest_name);
	unlink(new_dir);
	return 0;
}

int
digest_post(ent, fhdr, direct)
int ent;
struct fileheader *fhdr;
char *direct;
{

	if (!IScurrBM) {
		return DONOTHING;
	}
	if (digestmode != NA)
		return DONOTHING;
	if (fhdr->accessed & FH_DIGEST) {
		dele_digest(fhdr->filetime, direct);
		tracelog("%s undigest %s %s %s", currentuser->userid, currboard,
			 fhdr->owner, fhdr->title);
	} else {
		struct fileheader digest;
		char digestdir[STRLEN], digestfile[STRLEN], oldfile[STRLEN];
		directfile(digestdir, direct, DIGEST_DIR);
		if (get_num_records(digestdir, sizeof (struct fileheader)) >
		    MAX_DIGEST) {
			move(3, 0);
			clrtobot();
			move(4, 10);
			prints
			    ("抱歉，你的文摘文章已经超过 %d 篇，无法再加入...\n",
			     MAX_DIGEST);
			pressanykey();
			return PARTUPDATE;
		}
		digest = *fhdr;
		digest.accessed |= FH_ISDIGEST;
		directfile(digestfile, direct, fh2fname(&digest));
		directfile(oldfile, direct, fh2fname(fhdr));
		if (!file_isfile(digestfile)) {
			digest.accessed = FH_ISDIGEST;
			link(oldfile, digestfile);
			append_record(digestdir, &digest,
				      sizeof (struct fileheader));
			tracelog("%s digest %s %s %s", currentuser->userid,
				 currboard, fhdr->owner, fhdr->title);
		}
	}
	change_dir(direct, fhdr, (void *) DIR_do_digest, ent, digestmode, 0,
		   NULL);
	return PARTUPDATE;
}

#ifndef NOREPLY
int
do_reply(fh)
struct fileheader *fh;
{
	if (fh->accessed & FH_INND || strchr(fh->owner, '.'))
		local_article = 0;
	else
		local_article = 1;
	return post_article(fh);
}
#endif

static int
garbage_line(str)
char *str;
{
	int qlevel = 0;
	while (*str == ':' || *str == '>') {
		str++;
		if (*str == ' ')
			str++;
		if (qlevel++ >= 1)
			return 1;
	}
	while (*str == ' ' || *str == '\t')
		str++;
	if (qlevel >= 1)
		if (strstr(str, "提到:\n")
		    || strstr(str, ": 】\n")
		    || strncmp(str, "==>", 3) == 0 || strstr(str, "的文章 □"))
			return 1;
	return ((*str == '\n') || (*str == '\r'));
}

int
transferattach(char *buf, int size, FILE * fp, FILE * fpto)
{
	if (!strncmp(buf, "begin 644 ", 10)) {
		fwrite(buf, 1, strlen(buf), fpto);
		while (fgets(buf, size, fp) != NULL) {
			fwrite(buf, 1, strlen(buf), fpto);
			if (!strncmp(buf, "end", 3))
				break;
		}
		return 1;
	}
	if (!strncmp(buf, "beginbinaryattach ", 18)) {
		char ch;
		unsigned int len;
		int n;
		fwrite(buf, 1, strlen(buf), fpto);
		fread(&ch, 1, 1, fp);
		if (ch != 0) {
			ungetc(ch, fp);
			return 0;
		}
		fwrite(&ch, 1, 1, fpto);
		fread(&len, 4, 1, fp);
		fwrite(&len, 4, 1, fpto);
		len = ntohl(len);
		while (len > 0) {
			n = (len > size) ? size : len;
			n = fread(buf, 1, n, fp);
			if (n <= 0) {
				fseek(fpto, len, SEEK_CUR);
				break;
			}
			fwrite(buf, 1, n, fpto);
			len -= n;
		}
		return 1;
	}
	return 0;
}

static int
skipattach(char *buf, int size, FILE * fp)
{
	if (!strncmp(buf, "begin 644 ", 10)) {
		while (fgets(buf, size, fp) != NULL)
			if (!strncmp(buf, "end", 3))
				break;
		return 1;
	}
	if (!strncmp(buf, "beginbinaryattach ", 18)) {
		char ch;
		unsigned int len;
		fread(&ch, 1, 1, fp);
		if (ch != 0) {
			ungetc(ch, fp);
			return 0;
		}
		fread(&len, 4, 1, fp);
		len = ntohl(len);
		fseek(fp, len, SEEK_CUR);
		return 1;
	}
	return 0;
}

/* When there is an old article that can be included -jjyang */
void
do_quote(filepath, quote_mode)
char *filepath;
char quote_mode;
{
	FILE *inf, *outf;
	char *qfile, *quser;
	char buf[512], *ptr;
	int bflag;
	int line_count = 0, newline = 1, garbageline = 0;
	qfile = quote_file;
	quser = quote_user;
	bflag = strncmp(qfile, "mail", 4);
	outf = fopen(filepath, "w");
	if (quote_mode == 'N') {
		fprintf(outf, "\n");
	} else if (*qfile && (inf = fopen(qfile, "r")) != NULL) {
		fgets(buf, sizeof (buf), inf);
		if ((ptr = strrchr(buf, ')')) != NULL) {
			ptr[1] = '\0';
			if ((ptr = strchr(buf, ':')) != NULL) {
				quser = ptr + 1;
				while (*quser == ' ')
					quser++;
			}
		}

		if (bflag)
			fprintf(outf,
				"\n【 在 %-.55s 的大作中提到: 】\n", quser);
		else
			fprintf(outf,
				"\n【 在 %-.55s 的来信中提到: 】\n", quser);
		if (quote_mode == 'A') {
			while (fgets(buf, sizeof (buf), inf) != NULL) {
				if (skipattach(buf, sizeof (buf), inf)) {
					newline = 1;
					continue;
				}
				fprintf(outf, "%s%s", newline ? ": " : "", buf);
				newline = strchr(buf, '\n') ? 1 : 0;
			}
		} else if (quote_mode == 'R') {
			while (fgets(buf, sizeof (buf), inf) != NULL)
				if (buf[0] == '\n')
					break;
			while (fgets(buf, sizeof (buf), inf) != NULL) {
				if (skipattach(buf, sizeof (buf), inf))
					continue;
				if (Origin2(buf))
					continue;
				fprintf(outf, "%s", buf);
			}
		} else if (quote_mode == 'Y') {
			while (fgets(buf, sizeof (buf), inf) != NULL)
				if (buf[0] == '\n')
					break;
			while (fgets(buf, sizeof (buf), inf) != NULL) {
				if (skipattach(buf, sizeof (buf), inf)) {
					newline = 1;
					continue;
				}
				if (strcmp(buf, "--\n") == 0)
					break;
				if (!newline && !garbageline) {
					fprintf(outf, "%s", buf);
				} else if (newline) {
					garbageline = garbage_line(buf);
					if (!garbageline)
						fprintf(outf, ": %s", buf);
				}
				newline = strchr(buf, '\n') ? 1 : 0;
			}
		} else {
			int max_line_count = 5;	//Assume at most 50 chars for each line
			while (fgets(buf, sizeof (buf), inf) != NULL)
				if (buf[0] == '\n')
					break;
			while (fgets
			       (buf,
				min((max_line_count - line_count) * 50,
				    sizeof (buf)), inf) != NULL) {
				if (strcmp(buf, "--\n") == 0)
					break;
				if (skipattach(buf, sizeof (buf), inf)) {
					newline = 1;
					continue;
				}
				if (!newline && !garbageline) {
					fprintf(outf, "%s", buf);
					line_count += strlen(buf) / 50 + 1;
				} else if (newline) {
					garbageline = garbage_line(buf);
					if (!garbageline) {
						fprintf(outf, ": %s",
							void1(buf));
						line_count +=
						    strlen(buf) / 50 + 1;
					}
				}
				newline = strchr(buf, '\n') ? 1 : 0;
				if (line_count >= max_line_count) {
					if (!newline)
						fprintf(outf, "\n");
					fprintf(outf, ": ...................");
					break;
				}
				newline = strchr(buf, '\n') ? 1 : 0;
			}
		}
		fprintf(outf, "\n");
		fclose(inf);
	}
	*quote_file = '\0';
	*quote_user = '\0';
	if (!(uinfo.signature == 0 || header.chk_anony == 1)) {
		addsignature(outf, 0);
	}
	fclose(outf);
}

/* Add by SmallPig */
static void
getcross(filepath, mode, hide_bname, old_fh)
char *filepath;
int mode;
int hide_bname;
struct fileheader *old_fh;
{
	FILE *inf, *of;
	char buf[256];
	int count;
	time_t now;
	int hashead = 1;
	now = time(NULL);
	inf = fopen(quote_file, "r");
	of = fopen(filepath, "w");
	if (inf == NULL || of == NULL) {
		if (inf)
			fclose(inf);
		if (of)
			fclose(of);
		errlog("Cross Post error");
		return;
	}
	if (mode == 0) {
		if (in_mail == YEA) {
			in_mail = NA;
			write_header(of, 0 /*不写入 .posts */ );
			in_mail = YEA;
		} else
			write_header(of, 0 /*不写入 .posts */ );
		if (in_mail == YEA)
			fprintf(of,
				"\033[m\033[1m【 以下文字转载自 \033[32m%s \033[m\033[1m的信箱 】\n",
				currentuser->userid);
		else
			fprintf(of,
				"\033[m\033[1m【 以下文字转载自 \033[32m%s \033[m\033[1m讨论区 】\n",
				hide_bname ? "隐藏" : quote_board);
		if (old_fh != NULL)
			fprintf(of,
				"\033[m\033[1m【 原文由\033[32m %s\033[m\033[1m 于 \033[0m%.24s\033[1m 发表 】\033[m\n",
				fh2owner(old_fh), Ctime(old_fh->filetime));
		if (fgets(buf, 256, inf) != NULL) {
			if ((in_mail && strncmp(buf, "寄信人: ", 8))
			    || (!in_mail && strncmp(buf, "发信人: ", 8))) {
				hashead = 0;
			}
		}
		if (hashead) {
			while (fgets(buf, 256, inf) != NULL)	/*Clear Post header */
				if (buf[0] == '\n' || buf[0] == '\r')
					break;
		} else {
			fseek(inf, 0, SEEK_SET);
		}
	} else if (mode == 1) {
		fprintf(of, "发信人: deliver (自动发信系统), 信区: %s\n",
			quote_board);
		fprintf(of, "标  题: %s\n", quote_title);
		fprintf(of, "发信站: %s自动发信系统 (%24.24s)\n\n",
			MY_BBS_NAME, ctime(&now));
		fprintf(of, "【此篇文章是由自动发信系统所张贴】\n\n");
		//} else if (mode == 2) {
	} else {
		write_header(of, 0 /*写入 .posts */ );
	}

	while ((count = fread(buf, 1, sizeof (buf), inf)) > 0)
		fwrite(buf, 1, count, of);
	fclose(inf);
	fclose(of);
	*quote_file = '\0';
}

int
do_post()
{
	*quote_file = '\0';
	*quote_user = '\0';
	local_article = 0;
	return post_article(NULL);
}

/* Add by SmallPig */
static int
post_cross(bname, mode, islocal, hascheck, dangerous, hide_bname, old_fh)
char *bname;
int mode;
int islocal;
int hascheck;
int dangerous;
int hide_bname;
struct fileheader *old_fh;
{
	struct fileheader postfile;
	char filepath[STRLEN], fname[STRLEN];
	char buf[256], buf4[STRLEN], whopost[IDLEN + 2];
	char tmpfn[STRLEN];
	char bkcurrboard[STRLEN];
	int fp, count, ddigestmode;
	time_t now;
	int bnum;
	bnum = getbnum(bname);
	if (bnum <= 0 && !mode) {
		clear();
		move(1, 0);
		prints("看板 %s 并不存在，无法张贴.", bname);
		pressreturn();
		return -1;
	}
	if (!mode && !haspostperm(bnum)) {
		clear();
		move(1, 0);
		prints("您尚无权限在 %s 发表文章.\n", bname);
		return -1;
	}
	if (!mode && noadm4political(bnum)) {
		clear();
		move(1, 0);
		prints("对不起,因为没有版面管理人员在线,本版暂时封闭.");
		return -1;
	}
	if (!mode && bbsinfo.bcache[bnum - 1].ban == 2) {
		clear();
		move(1, 0);
		prints("对不起, 版面文章超限, 本版暂时封闭.");
		return -1;
	}
	bzero(&postfile, sizeof (struct fileheader));
	now = time(NULL);
	sprintf(fname, "M.%d.A", (int) now);
	if (!mode) {
		if (!strstr(quote_title, "[转载]"))
			sprintf(buf4, "[转载] %.70s", quote_title);
		else
			strcpy(buf4, quote_title);
	} else
		strcpy(buf4, quote_title);
	strncpy(save_title, buf4, STRLEN);
	save_title[STRLEN - 1] = 0;
	setbfile(filepath, bname, fname);
	count = 0;
	while ((fp = open(filepath, O_CREAT | O_EXCL | O_WRONLY, 0660)) == -1) {
		now++;
		sprintf(fname, "M.%d.A", (int) now);
		setbfile(filepath, bname, fname);
		if (count++ > MAX_POSTRETRY) {
			return -1;
		}
	}
	close(fp);
	postfile.filetime = now;
	postfile.thread = postfile.filetime;
	if ((!islocal) && (mode != 1)) {
		postfile.accessed |= FH_INND;
		local_article = 0;
	} else
		local_article = 1;
	if (mode == 1)
		strcpy(whopost, "deliver");
	else
		strcpy(whopost, currentuser->userid);
/*	if (mode == 1)
		postfile.accessed |= FH_MARKED;*/
	if (old_fh && old_fh->accessed & FH_MATH)
		postfile.accessed |= FH_MATH;
	strsncpy(postfile.owner, whopost, sizeof (postfile.owner));
	setbfile(filepath, bname, fname);
	modify_user_mode(POSTING);
	strcpy(bkcurrboard, currboard);
	strcpy(currboard, bname);
	getcross(filepath, mode, hide_bname, old_fh);
	strcpy(currboard, bkcurrboard);
	postfile.sizebyte = numbyte(eff_size(filepath));
	if (eff_size_isjunk)
		postfile.accessed |= FH_DEL;
	strsncpy(postfile.title, save_title, sizeof (postfile.title));
	if (mode != 1) {
		if (!hascheck) {
			sprintf(tmpfn, MY_BBS_HOME "/bbstmpfs/tmp/filter.%s.%d",
				currentuser->userid, getpid());
			copyfile(filepath, tmpfn);
			filter_attach(tmpfn);
			dangerous =
			    dofilter(postfile.title, tmpfn,
				     political_board(bname, 0));
			unlink(tmpfn);
		}
		if (dangerous) {
#ifdef POST_WARNING
			char mtitle[256];
			snprintf(mtitle, sizeof (mtitle), "[转载报警] %s %.60s",
				 bname, postfile.title);
			system_mail_file(filepath, "delete", mtitle,
					 currentuser->userid);
			updatelastpost("deleterequest");
#endif
			postfile.accessed |= FH_DANGEROUS;
		}
	}
	{
		int i = strlen(postfile.title) - 1;
		while (i > 0 && isspace(postfile.title[i]))
			postfile.title[i--] = 0;
	}

	ddigestmode = digestmode;
	digestmode = 0;
	setbdir(buf, bname, digestmode);
	digestmode = ddigestmode;
	if (append_record(buf, &postfile, sizeof (struct fileheader)) == -1) {
		if (!mode) {
			errlog
			    ("cross_posting '%s' on '%s': append_record failed!",
			     postfile.title, quote_board);
		} else {
			errlog
			    ("Posting '%s' on '%s': append_record failed!",
			     postfile.title, quote_board);
		}
		pressreturn();
		clear();
		return 1;
	}
	if (!hideboard(bname))
		log_article(&postfile, filepath, bname);
	if (innd_board(bname))
		outgo_post(&postfile, bname, currentuser->userid,
			   currentuser->username);
	updatelastpost(bname);
	if (!mode || mode == 3) {
		add_crossinfo(filepath, 1);
		tracelog("%s crosspost %s %s", currentuser->userid, bname,
			 postfile.title);
	}
	return 1;
}

void
add_loginfo(filepath)
char *filepath;
{
	FILE *fp;
	int color, noidboard;
	char fname[STRLEN];
	noidboard = header.chk_anony;
	color = (rand() % 7) + 31;
	setuserfile(fname, "signatures");
	if ((fp = fopen(filepath, "a")) == NULL)
		return;
	if (!file_isfile(fname) || uinfo.signature == 0 || noidboard)
		fputs("\n--", fp);
	fprintf(fp,
		"\n\033[m\033[1;%2dm※ 来源:．%s %s．[FROM: %-.20s]\033[m\n",
		color, MY_BBS_NAME, email_domain(),
		(noidboard) ? "匿名天使的家" : fromhost);
	fclose(fp);
	return;
}

void
add_crossinfo(filepath, mode)
char *filepath;
int mode;
{
	FILE *fp;
	int color;
	color = (currentuser->numlogins % 7) + 31;
	if ((fp = fopen(filepath, "a")) == NULL)
		return;
	fprintf(fp,
		"--\n\033[m\033[1;%2dm※ 转%s:．%s %s．[FROM: %-.20s]\033[m\n",
		color, (mode == 1) ? "载" : "寄",
		MY_BBS_NAME, email_domain(), fromhost);
	fclose(fp);
	return;
}

int
show_board_notes(bname)
char bname[30];
{
	char buf[256];
	move(2, 0);
	sprintf(buf, "vote/%s/notes", bname);
	if (file_isfile(buf)) {
		ansimore2stuff(buf, NA, 2, 24);
		return 1;
	} else if (file_isfile("vote/notes")) {
		ansimore2stuff("vote/notes", NA, 2, 24);
		return 1;
	}
	return -1;
}

static int
post_article(struct fileheader *sfh)
{
	struct fileheader postfile;
	char filepath[STRLEN], buf[STRLEN], edittmp[STRLEN];
	char newfilepath[STRLEN], newfname[STRLEN];
	int retv;
	int bnum;
	time_t t;
	char *replytitle;
	struct userec tmpu;
	if (sfh == NULL)
		replytitle = NULL;
	else
		replytitle = sfh->title;
	modify_user_mode(POSTING);
	bnum = getbnum(currboard);
	if (!strcmp(MY_BBS_ID, "TTTAN") && !strcasecmp(currboard, "sex")
	    && !strcasecmp(currentuser->userid, "peaches"))
		goto PASSPEACHES;

	if (!haspostperm(bnum)) {
		move(3, 0);
		clrtobot();
		if (digestmode == NA) {
			prints
			    ("\n\n        此讨论区是唯读的, 或是您尚无权限在此发表文章。");
		} else {
			prints
			    ("\n\n     目前是文摘或主题模式, 所以不能发表文章 (按左键可离开此模式)。");
		}
		pressreturn();
		clear();
		return FULLUPDATE;
	}

      PASSPEACHES:

	if (noadm4political(bnum)) {
		move(3, 0);
		clrtobot();
		prints
		    ("\n\n               对不起,因为没有版面管理人员在线,本版暂时封闭.");
		pressreturn();
		clear();
		return FULLUPDATE;
	}
	if (bbsinfo.bcache[bnum - 1].ban == 2) {
		move(3, 0);
		clrtobot();
		prints("\t对不起, 版面文章超限, 本版暂时封闭.");
		pressreturn();
		return FULLUPDATE;
	}

	if (club_board(currboard, bnum)) {
		if (!clubtest(currboard) && !USERPERM(currentuser, PERM_SYSOP)) {
			move(3, 0);
			clrtobot();
			prints
			    ("\n\n             %s为俱乐部版面，请向版务申请发文权限",
			     currboard);
			pressreturn();
			clear();
			return FULLUPDATE;
		}
	}
	if (deny_me(currboard) && !USERPERM(currentuser, PERM_SYSOP)) {
		move(3, 0);
		clrtobot();
		prints
		    ("\n\n                 很抱歉，你被版主停止 POST 的权利。");
		pressreturn();
		clear();
		return FULLUPDATE;
	}
	if (deny_me_global()
	    && strcmp(currboard, "sysop")
	    && strcmp(currboard, "Arbitration")
	    && !(bbsinfo.bcache[getbnum(currboard) - 1].header.flag & CLOSECLUB_FLAG)
	    && !USERPERM(currentuser, PERM_SYSOP)) {
		move(3, 0);
		clrtobot();
		prints
		    ("\n\n                 很抱歉，你被站务停止全站 POST 的权利。");
		pressreturn();
		clear();
		return FULLUPDATE;
	}

	bzero(&postfile, sizeof (struct fileheader));
	clear();
	if (!innd_board(currboard) && !njuinn_board(currboard))
		local_article = -1;
	else if (!innd_board(currboard))
		local_article = 1;

	do_delay(1);		/*by ylsdd */
	show_board_notes(currboard);
#ifndef NOREPLY
	if (replytitle != NULL) {
		if (strncasecmp(replytitle, "Re:", 3) == 0)
			strcpy(header.title, replytitle);
		else
			snprintf(header.title, STRLEN - 1, "Re: %s",
				 replytitle);
		header.reply_mode = 1;
	} else
#endif
	{
		header.title[0] = '\0';
		header.reply_mode = 0;
	}
	strcpy(header.ds, currboard);
	header.postboard = YEA;
	{
		int i = strlen(header.title) - 1;
		while (i > 0 && isspace(header.title[i]))
			header.title[i--] = 0;
	}
	if (post_header(&header)) {
		strsncpy(postfile.title, header.title, sizeof (postfile.title));
		strsncpy(save_title, postfile.title, sizeof (save_title));
	} else {
		do_delay(-1);	/* by ylsdd */
		return FULLUPDATE;
	}
	setbfile(filepath, currboard, "");
	t = trycreatefile(filepath, "M.%d.A", now_t, 100);
	if (t < 0)
		return -1;
	postfile.filetime = t;
	if (sfh != NULL)
		postfile.thread = sfh->thread;
	fh_setowner(&postfile, currentuser->userid, header.chk_anony);
	modify_user_mode(POSTING);

	sprintf(edittmp, MY_BBS_HOME "/bbstmpfs/tmp/%s.%d",
		currentuser->userid, getpid());
	do_quote(edittmp, header.include_mode);
	retv = vedit(edittmp, YEA, YEA);

	/*Anony=0; *//*Inital For ShowOut Signature */
	if ((retv == REAL_ABORT)) {
		unlink(edittmp);
		unlink(filepath);
		clear();
		do_delay(-1);	/* by ylsdd */
/*        pressreturn() ;*/
		return FULLUPDATE;
	}
	if (retv == POST_NOREPLY) {
		postfile.accessed |= FH_NOREPLY;
	}
	add_loginfo(edittmp);
	postfile.sizebyte = numbyte(eff_size(edittmp));
	if (eff_size_isjunk)
		postfile.accessed |= FH_DEL;

	crossfs_rename(edittmp, filepath);

	if (local_article == 0)
		postfile.accessed |= FH_INND;
	strsncpy(postfile.title, save_title, sizeof (postfile.title));

	if (!hideboard(currboard)) {
		int dangerous;
		dangerous =
		    dofilter(postfile.title, filepath,
			     political_board(currboard, 0));
		switch (dangerous) {
#ifdef POST_WARNING
			char mtitle[256];
#endif
		case -1:
			post_to_1984(filepath, &postfile, 0);
			unlink(filepath);
			clear();
			do_delay(-1);
			return FULLUPDATE;
		case -2:
#ifdef POST_WARNING
			snprintf(mtitle, sizeof (mtitle),
				 "[发表报警] %s %.60s", currboard,
				 postfile.title);
			system_mail_file(filepath, "delete", mtitle,
					 currentuser->userid);
			updatelastpost("deleterequest");
#endif
			postfile.accessed |= FH_DANGEROUS;
			break;
		default:
			break;
		}
	}
	if (is1984_board(currboard)) {
		post_to_1984(filepath, &postfile, 0);
		unlink(filepath);
		if (!junkboard()) {
			memcpy(&tmpu, currentuser, sizeof (struct userec));
			tmpu.numposts++;
			updateuserec(&tmpu, usernum);
		}
		move(0, 0);
		clear();
		prints("%s", DO1984_NOTICE);
		pressanykey();
		return FULLUPDATE;
	}

	/*重新指定文件名 */
	if (1) {
		int count;
		t = time(NULL);
		count = 0;
		while (1) {
			sprintf(newfname, "M.%d.A", (int) t);
			setbfile(newfilepath, currboard, newfname);
			if (link(filepath, newfilepath) == 0) {
				unlink(filepath);
				postfile.filetime = t;
				break;
			}
			t++;
			if (count++ > MAX_POSTRETRY)
				break;
		}
	}

	if (sfh == NULL)
		postfile.thread = postfile.filetime;
	setbdir(buf, currboard, digestmode);
	if (append_record(buf, &postfile, sizeof (struct fileheader)) == -1) {
		errlog
		    ("posting '%s' on '%s': append_record failed!",
		     postfile.title, currboard);
		pressreturn();
		clear();
		return FULLUPDATE;
	}
	if (local_article == 0 && innd_board(currboard))
		outgo_post(&postfile, currboard, currentuser->userid,
			   currentuser->username);
	local_article = 0;
	SETREAD(&postfile, &brc);
	updatelastpost(currboard);
	tracelog("%s post %s %s", currentuser->userid, currboard,
		 postfile.title);
	if (!hideboard(currboard))
		log_article(&postfile, newfilepath, currboard);
	if (!junkboard()) {
		memcpy(&tmpu, currentuser, sizeof (struct userec));
		tmpu.numposts++;
		updateuserec(&tmpu, usernum);
	}
	return DIRCHANGED;
}

static int
dofilter(title, fn, mode)
char *title, *fn;
int mode;			// 1: politics    0: non-politics 
{
	//this function return 0 if article is safe, return -1 if article is dangerous
	//return -2 if article is possbily dangerous.
	struct mmapfile *mf;
	char *bf;
	switch (mode) {
	case 1:
		mf = &mf_badwords;
		bf = BADWORDS;
		break;
	case 0:
		mf = &mf_sbadwords;
		bf = SBADWORDS;
		break;
	case 2:
		mf = &mf_pbadwords;
		bf = PBADWORDS;
		break;
	default:
		return -1;
	}
	if (mmapfile(bf, mf) < 0)
		goto CHECK2;
	if (filter_article(title, fn, mf)) {
		if (mode != 2) {
			move(0, 0);
			clear();
			prints("%s", BAD_WORD_NOTICE);
			pressanykey();
			return -1;
		}
		return -2;
	}
      CHECK2:
	if (1 != mode)
		return 0;
	mf = &mf_pbadwords;
	bf = PBADWORDS;
	if (mmapfile(bf, mf) < 0)
		return 0;
	if (filter_article(title, fn, mf))
		return -2;
	else
		return 0;
}

int
stringfilter(title, mode)
char *title;
{
	struct mmapfile *mf;
	char *bf;
	switch (mode) {
	case 1:
		mf = &mf_badwords;
		bf = BADWORDS;
		break;
	case 0:
		mf = &mf_sbadwords;
		bf = SBADWORDS;
		break;
	case 2:
		mf = &mf_pbadwords;
		bf = PBADWORDS;
		break;
	default:
		return -1;
	}
	if (mmapfile(bf, mf) < 0)
		return 0;
	if (filter_string(title, mf)) {
		return -1;
	}
	return 0;
}

static int
change_content_title(fname, title)
char *fname;
char *title;
{
	FILE *fp, *out;
	char buf[256];
	char outname[STRLEN];
	int has_changed_t = 0;
	if ((fp = fopen(fname, "r")) == NULL)
		return 0;
	sprintf(outname, "bbstmpfs/tmp/editpost.%s.%05d",
		currentuser->userid, uinfo.pid);
	if ((out = fopen(outname, "w")) == NULL) {
		fclose(fp);
		return 0;
	}
	while ((fgets(buf, 256, fp)) != NULL) {
		if (transferattach(buf, sizeof (buf), fp, out))
			continue;
		if (!has_changed_t && !strncmp(buf, "标  题: ", 8)) {
			fprintf(out, "标  题: %s\n", title);
			has_changed_t = 1;
			continue;
		}
		fputs(buf, out);
	}
	fclose(fp);
	fclose(out);
	copyfile(outname, fname);
	unlink(outname);
	return 0;
}
 /*ARGSUSED*/ int
edit_post(ent, fileinfo, direct)
int ent;
struct fileheader *fileinfo;
char *direct;
{
	extern char currmaildir[STRLEN];
	char filepath[STRLEN];
	char tmpfile[STRLEN];
	char attach_path[256];
	int bnum;
	bnum = getbnum(currboard);
	if (bnum <= 0)
		return DONOTHING;
	if (!in_mail) {
		if (digestmode == 4 || digestmode == 5)
			return DONOTHING;
		if (!strcmp(currboard, "syssecurity"))
			return DONOTHING;
		if (!IScurrBM && !isowner(currentuser, fileinfo))
			return DONOTHING;
		if (is1984_board(currboard))
			return DONOTHING;
		if (!haspostperm(bnum))
			return DONOTHING;
	}

	if (in_mail)
		directfile(filepath, currmaildir, fh2fname(fileinfo));
	else
		directfile(filepath, direct, fh2fname(fileinfo));
	modify_user_mode(EDIT);
	clear();
	if (!file_isfile(filepath)) {
		return FULLUPDATE;
	}
	sprintf(tmpfile, "boards/.tmp/editpost.%s.%05d",
		currentuser->userid, uinfo.pid);
	copyfile_attach(filepath, tmpfile);
	if (vedit(tmpfile, NA, NA) == -1) {
		unlink(tmpfile);
		return FULLUPDATE;
	}
	if (!in_mail && !hideboard(currboard)) {
		int dangerous = dofilter(fileinfo->title, tmpfile,
					 political_board(currboard, 0));
		switch (dangerous) {
#ifdef POST_WARNING
			char mtitle[256];
#endif
		case -1:
			fileinfo->sizebyte = numbyte(eff_size(tmpfile));
			fileinfo->edittime = time(0);
			post_to_1984(tmpfile, fileinfo, 0);
			unlink(tmpfile);
			return FULLUPDATE;
		case -2:
#ifdef POST_WARNING
			snprintf(mtitle, sizeof (mtitle),
				 "[修改报警] %s %.60s", currboard,
				 fileinfo->title);
#endif
			change_dir(direct, fileinfo,
				   (void *) DIR_do_dangerous, ent,
				   digestmode, 1, NULL);
#ifdef POST_WARNING
			system_mail_file(tmpfile, "delete", mtitle,
					 currentuser->userid);
			updatelastpost("deleterequest");
#endif
			break;
		default:
			break;
		}
	}
	snprintf(attach_path, sizeof (attach_path),
		 PATHUSERATTACH "/%s", currentuser->userid);
	clearpath(attach_path);

	decode_attach(filepath, attach_path);
	insertattachments_byfile(filepath, tmpfile, currentuser->userid);
	unlink(tmpfile);
	fileinfo->sizebyte = numbyte(eff_size(filepath));
	fileinfo->edittime = time(0);
	if (in_mail)
		change_dir(direct, fileinfo, (void *) DIR_do_editmail, ent,
			   digestmode, 1, currentuser->userid);
	else
		change_dir(direct, fileinfo, (void *) DIR_do_edit, ent,
			   digestmode, 1, NULL);
	if (!in_mail) {
		if (innd_board(currboard))
			outgo_post(fileinfo, currboard, currentuser->userid,
				   currentuser->username);
		updatelastpost(currboard);
		tracelog("%s edit %s %s %s", currentuser->userid, currboard,
			 fh2owner(fileinfo), fileinfo->title);
		SETREAD(fileinfo, &brc);
	}
	if (ADD_EDITMARK || in_mail)
		add_edit_mark(filepath, currentuser->userid,
			      fileinfo->edittime, fromhost);
	return FULLUPDATE;
}

static int
edit_title(ent, fileinfo, direct)
int ent;
struct fileheader *fileinfo;
char *direct;
{
	char buf[STRLEN];
	int now;
	int bnum;
	if (is1984_board(currboard))
		return DONOTHING;
	bnum = getbnum(currboard);
	if (bnum <= 0)
		return DONOTHING;
	if (!IScurrBM) {
		if (digestmode == 4 || digestmode == 5
		    || !strcmp(currboard, "syssecurity"))
			return DONOTHING;
		if (!isowner(currentuser, fileinfo))
			return DONOTHING;
		if (!haspostperm(bnum))
			return DONOTHING;
	}
	strsncpy(buf, fileinfo->title, sizeof (buf));
	getdata(t_lines - 1, 0, "新文章标题: ", buf, 50, DOECHO, NA);

	if (buf[0] != '\0' && strcmp(fileinfo->title, buf)
	    && (!stringfilter(buf, politics(currboard)))) {
		tracelog("%s changetitle %s %s oldtitle:%s newtitle:%s",
			 currentuser->userid, currboard,
			 fh2owner(fileinfo), fileinfo->title, buf);
		strsncpy(fileinfo->title, buf, sizeof (fileinfo->title));
		directfile(genbuf, direct, fh2fname(fileinfo));
		change_content_title(genbuf, buf);
		now = time(NULL);
		add_edit_mark(genbuf, currentuser->userid, now, fromhost);
		change_dir(direct, fileinfo,
			   (void *) DIR_do_changetitle, ent, digestmode, 1,
			   NULL);
	}
	return PARTUPDATE;
}

int
mark_post(ent, fileinfo, direct)
int ent;
struct fileheader *fileinfo;
char *direct;
{
	if (!IScurrBM) {
		return DONOTHING;
	}
	if (fileinfo->accessed & FH_MARKED) {
		tracelog("%s unmark %s %s %s", currentuser->userid, currboard,
			 fh2owner(fileinfo), fileinfo->title);
	} else {
		tracelog("%s mark %s %s %s", currentuser->userid, currboard,
			 fh2owner(fileinfo), fileinfo->title);
	}
#ifdef ENABLE_MYSQL
	marked_eva(ent, fileinfo, direct,
		   (fileinfo->accessed & FH_MARKED) ? 0 : 1);
#endif
	change_dir(direct, fileinfo, (void *) DIR_do_mark, ent, digestmode, 0,
		   NULL);
	return PARTUPDATE;
}

int
markdel_post(ent, fileinfo, direct)
int ent;
struct fileheader *fileinfo;
char *direct;
{
	if (!strcmp(currboard, "deleted")
	    || !strcmp(currboard, "junk") || (!IScurrBM && !ISdelrq))
		return DONOTHING;
	if (fileinfo->owner[0] == '-')
		return PARTUPDATE;
	change_dir(direct, fileinfo, (void *) DIR_do_markdel, ent,
		   digestmode, 0, NULL);
	return PARTUPDATE;
}

int
addtop_post(ent, fileinfo, direct)
int ent;
struct fileheader *fileinfo;
char *direct;
{
	int bnum, i;
	struct boardaux *baux;
	char ans[4];
	if (!strcmp(currboard, "deleted")
	    || !strcmp(currboard, "junk") || !IScurrBM)
		return DONOTHING;
	bnum = getbnum(currboard);
	if (bnum <= 0)
		return DONOTHING;
	bnum--;			//getboardaux参数从0开始
	if ((baux = getboardaux(bnum)) == NULL)
		return DONOTHING;
	move(0, 0);
	clrtobot();
	prints("当前文章：\n\t(\033[32mA\033[m): %s\n", fileinfo->title);
	prints("已有置顶(低)文章 %d 篇（最多 3 篇）：\n", baux->ntopfile);
	for (i = 0; i < baux->ntopfile; i++)
		prints("\t(\033[32m%d\033[m): %s\n", i, baux->topfile[i].title);
	getdata(t_lines - 1, 0, "输入 A 将当前文章置顶；输入编号删除置顶：",
		ans, 2, DOECHO, YEA);
	if (toupper(ans[0]) == 'A')
		addtopfile(bnum, fileinfo);
	else if (isdigit(ans[0]))
		deltopfile(bnum, ans[0] - '0');
	return DIRCHANGED;
}

static int
markspec_post(ent, fileinfo, direct)
int ent;
struct fileheader *fileinfo;
char *direct;
{
	if (!strcmp(currboard, "deleted")
	    || !strcmp(currboard, "junk") || !IScurrBM)
		return DONOTHING;
	change_dir(direct, fileinfo, (void *) DIR_do_spec, ent, digestmode, 0,
		   NULL);
	return PARTUPDATE;
}

static int
import_spec()
{
	int fd;
	int put_announce_flag;
	char direct[STRLEN];
	struct fileheader fileinfo;
	char anboard[STRLEN], tmpboard[STRLEN];
//   if(strcmp(currentuser->userid,"ecnegrevid")!=0) return DONOTHING;
	if (digestmode == 2 || digestmode == 3
	    || digestmode == 4 || digestmode == 5
	    || !strcmp(currboard, "deleted")
	    || !strcmp(currboard, "junk") || !IScurrBM)
		return DONOTHING;
	if (select_anpath() < 0 || check_import(anboard) < 0)
		return FULLUPDATE;
	setbdir(direct, currboard, digestmode);
	fd = open(direct, O_RDWR);
	if (fd == -1)
		return FULLUPDATE;
	put_announce_flag = !strcmp(currboard, anboard);
	strcpy(tmpboard, currboard);
	strcpy(currboard, anboard);
	while (read(fd, &fileinfo, sizeof (struct fileheader)) > 0) {
		if (!(fileinfo.accessed & FH_SPEC))
			continue;
		fileinfo.accessed &= ~FH_SPEC;
		if (put_announce_flag)
			fileinfo.accessed |= FH_ANNOUNCE;
		if (a_Import(direct, &fileinfo, YEA) < 0)
			break;
		if (lseek(fd, -sizeof (struct fileheader), SEEK_CUR) ==
		    (off_t) - 1)
			break;
		if (write(fd, &fileinfo, sizeof (struct fileheader)) < 0)
			break;
	}
	strcpy(currboard, tmpboard);
	close(fd);
	return DIRCHANGED;
}

static int
moveintobacknumber(ent, fileinfo, direct)
int ent;
struct fileheader *fileinfo;
char *direct;
{
	struct tm atm;
	time_t t;
	char buf[STRLEN], content[1024];
	struct boardmem *bmem;
	if (uinfo.mode != READING || digestmode != NA)
		return DONOTHING;
	if (!(bmem = getbcache(currboard)))
		return DONOTHING;
	if (!chk_editboardperm(&(bmem->header)))
		return DONOTHING;
	clear();
	bzero(&atm, sizeof (struct tm));
	prints
	    ("整理过刊, 请指定日期, 在该日期之前所发表的文章将被\n"
	     "迁移到最后一个过刊目录里, 而且不再存在于版面\n");
	if (askyn("要继续吗?", NA, NA) == NA)
		return FULLUPDATE;
	t = time(NULL);
	atm = *gmtime(&t);
	atm.tm_sec = 0;
	atm.tm_min = 0;
	atm.tm_hour = 0;
	while (1) {
		getdata(3, 0, "年: ", buf, 6, DOECHO, YEA);
		atm.tm_year = atoi(buf) - 1900;
		if (atm.tm_year + 1900 >= 1999)
			break;
		prints("不恰当的年份(1999～)");
	}
	while (1) {
		getdata(4, 0, "月: ", buf, 6, DOECHO, YEA);
		atm.tm_mon = atoi(buf) - 1;
		if (atm.tm_mon + 1 > 0 && atm.tm_mon + 1 <= 12)
			break;
		prints("不恰当的月份(1～12)");
	}
	while (1) {
		getdata(5, 0, "日: ", buf, 6, DOECHO, YEA);
		atm.tm_mday = atoi(buf);
		if (atm.tm_mday > 0 && atm.tm_mday <= 31)
			break;
		prints("不恰当的日期(1～31)");
	}
	t = mktime(&atm);
	if (t <= 0 || t >= (time(NULL) - (time_t) 3600 * 24 * 7)) {
		prints
		    ("时间指定有误( 您输入的时间为%s )\n(距当前日期一周之内的文章不能放到过刊的)\n",
		     Ctime(t));
		pressanykey();
		return FULLUPDATE;
	}
	prints("您所指定的时间为 %s", ctime(&t));
	if (askyn
	    ("确定要把该时间之前的文章放进过刊么(可能需要几分钟)?", NA,
	     NA) == YEA) {
		int retv;
		retv = do_intobacknumber(direct, t);
		updatelastpost(currboard);
		if (retv < 0) {
			prints("retv=%d", retv);
			pressanykey();
		}
		sprintf(genbuf, "into backnumber, %s, %s, %d",
			currboard, Ctime(t), retv);
		sprintf(content, "%s 将文章置入 %s 版过刊 ",
			currentuser->userid, currboard);
		securityreport(genbuf, content);
	}
	return FULLUPDATE;
}

int
del_range(ent, fileinfo, direct)
int ent;
struct fileheader *fileinfo;
char *direct;
{
	char num[16], content[1024];
	int inum1, inum2, ret;
	if (uinfo.mode == READING)
		if (!IScurrBM && !ISdelrq) {
			return DONOTHING;
		}
	//allow sysops to repair, but nobody can del_range
	//if(digestmode==4||digestmode==5) return DONOTHING;

	if (((digestmode >= 2 && digestmode <= 5)
	     || !strcmp(currboard, "syssecurity"))
	    && uinfo.mode == READING)
		return DONOTHING;
	clear();
	prints("区域删除\n");
	getdata(1, 0,
		"首篇文章编号(输入0清除标记为删除的文章): ",
		num, 6, DOECHO, YEA);
	inum1 = atoi(num);
	if (inum1 == 0) {
		inum2 = -1;
		goto THERE;
	}

	if (inum1 <= 0) {
		prints("错误编号\n");
		pressreturn();
		return FULLUPDATE;
	}
	getdata(2, 0, "末篇文章编号: ", num, 14, DOECHO, YEA);
	inum2 = atoi(num);
	if (inum2 - inum1 <= 1) {
		prints("错误编号\n");
		pressreturn();
		return FULLUPDATE;
	}
      THERE:
	move(3, 0);
	if (askyn("确定删除", NA, NA) == YEA) {
		if ((ret = delete_range(direct, inum1, inum2)) < 0) {
			char num1[20];
			char fullpath[200];
			prints("无法删除:%d\n", ret);
			getdata(8, 0,
				"区段删除错误,如果想修复,请确定\033[35m无人在本版执行区段删除操作并按'Y'\033[0m (Y/N)? [N]: ",
				num1, 10, DOECHO, YEA);
			if (*num1 == 'Y' || *num1 == 'y') {
				sprintf(fullpath, "boards/%s/.tmpfile",
					currboard);
				unlink(fullpath);
				sprintf(fullpath, "boards/%s/.deleted",
					currboard);
				unlink(fullpath);
				sprintf(fullpath, "boards/%s/.tmpfilD",
					currboard);
				unlink(fullpath);
				sprintf(fullpath, "boards/%s/.tmpfilJ",
					currboard);
				unlink(fullpath);
				prints("\n错误已经消除,请重新执行区段删除!");
			}

			pressreturn();
			return FULLUPDATE;
		}
		fixkeep(direct, (inum1 <= 0) ? 1 : inum1,
			(inum2 <= 0) ? 1 : inum2);
		if (uinfo.mode == READING) {
			updatelastpost(currboard);
			sprintf(genbuf, "Range delete %d-%d on %s",
				inum1, inum2, currboard);
			sprintf(content, "%s 区段删除 %s 版 %d-%d篇",
				currentuser->userid, currboard, inum1, inum2);
			securityreport(genbuf, content);
			tracelog("%s ranged %s %d %d", currentuser->userid,
				 currboard, inum1, inum2);
		} else {
			tracelog("%s rangedmail %d %d", currentuser->userid,
				 inum1, inum2);
		}
		prints("删除完成\n");
		pressreturn();
		return DIRCHANGED;
	}
	prints("Delete Aborted\n");
	pressreturn();
	return FULLUPDATE;
}

static int
del_post_backup(ent, fileinfo, direct)
int ent;
struct fileheader *fileinfo;
char *direct;
{
	int keep, fail;
	char filepath[STRLEN];
	if (digestmode == 2 || digestmode == 3
	    || digestmode == 4 || digestmode == 5
	    || !strcmp(currboard, "deleted")
	    || !strcmp(currboard, "junk")
	    || !strcmp(currboard, "syssecurity"))
		return DONOTHING;
	if (!strcmp(currboard, "deleterequest")) {
		if (!strncmp(fileinfo->title, "done", 4))
			return DONOTHING;
		snprintf(genbuf, 60, "done %-27.27s - %s",
			 fileinfo->title, currentuser->userid);
		strcpy(fileinfo->title, genbuf);
		change_dir(direct, fileinfo,
			   (void *) DIR_do_changetitle, ent, digestmode, 1,
			   NULL);
		return FULLUPDATE;
	}
	if (hideboard(currboard))
		return DONOTHING;
	if (fileinfo->owner[0] == '-')
		return PARTUPDATE;
	keep = sysconf_eval("KEEP_DELETED_HEADER");
	if (qnyjzx(currentuser->userid)) {
		if (!politics(currboard)) {
			return DONOTHING;
		}
	} else if (!ISdelrq)
		return DONOTHING;
	clear();
	sprintf(genbuf, "删除文章 [%-.55s]", fileinfo->title);
	if (askyn(genbuf, NA, NA) == NA) {
		if (fileinfo->accessed | FH_DANGEROUS) {
			sprintf(genbuf, "那要清除本文的危险标记嘛? [%-38s]",
				fileinfo->title);
			if (askyn(genbuf, YEA, NA) == YEA) {
				change_dir(direct, fileinfo,
					   (void *) DIR_clear_dangerous, ent,
					   digestmode, 1, NULL);
				prints("已经清除本文的危险标记\n");
				pressreturn();
				return FULLUPDATE;
			}
		}
		move(2, 0);
		prints("取消\n");
		pressreturn();
		clear();
		return FULLUPDATE;
	}
	tracelog("%s del %s %s %s", currentuser->userid, currboard,
		 fh2owner(fileinfo), fileinfo->title);
	currfiletime = fileinfo->filetime;
	setbfile(filepath, currboard, fh2fname(fileinfo));
	sprintf(fileinfo->title, "%-32.32s - %s", fileinfo->title,
		currentuser->userid);
	post_to_1984(filepath, fileinfo, 1);
	if (keep <= 0) {
		fail =
		    delete_file(direct, sizeof (struct fileheader),
				ent, (void *) cmpfilename);
	} else {
		fail =
		    update_file(direct, sizeof (struct fileheader),
				ent, (void *) cmpfilename,
				(void *) cpyfilename);
	}
	if (!fail) {
		updatelastpost(currboard);
		unlink(filepath);
		limit_cpu();
		return DIRCHANGED;
	}

	move(2, 0);
	prints("删除失败\n");
	pressreturn();
	clear();
	return FULLUPDATE;
}

int
del_post(ent, fileinfo, direct)
int ent;
struct fileheader *fileinfo;
char *direct;
{
	int keep, fail;
	int owned;
	struct boardmem *bp;
	struct userec tmpu;
	if (digestmode == 2 || digestmode == 3
	    || digestmode == 4 || digestmode == 5
	    || !strcmp(currboard, "deleted")
	    || !strcmp(currboard, "junk")
	    || !strcmp(currboard, "syssecurity"))
		return DONOTHING;
	if (!strcmp(MY_BBS_ID, "TTTAN")
	    && !strcasecmp(currentuser->userid, "peaches"))
		return DONOTHING;
	if (fileinfo->owner[0] == '-')
		return DONOTHING;
	bp = getbcache(currboard);
	if (bp == NULL)
		return DONOTHING;
	keep = sysconf_eval("KEEP_DELETED_HEADER");
	owned = isowner(currentuser, fileinfo);
	if (digestmode == 1)
		owned = 0;
	if (!owned && !IScurrBM)
		return DONOTHING;
	clear();
	sprintf(genbuf, "删除文章 [%-.55s]", fileinfo->title);
	if (askyn(genbuf, NA, NA) == NA) {
		move(2, 0);
		prints("取消\n");
		pressreturn();
		clear();
		return FULLUPDATE;
	}
	tracelog("%s del %s %s %s", currentuser->userid, currboard,
		 fh2owner(fileinfo), fileinfo->title);
	currfiletime = fileinfo->filetime;
	if (keep <= 0) {
		fail = delete_dir(direct, ent, fileinfo->filetime);
	} else {
		fail =
		    update_file(direct, sizeof (struct fileheader),
				ent, (void *) cmpfilename,
				(void *) cpyfilename);
	}
	if (fail) {
		move(2, 0);
		prints("删除失败\n");
		pressreturn();
		clear();
		return FULLUPDATE;
	}

	updatelastpost(currboard);
	cancelpost(currboard, currentuser->userid, fileinfo, owned);
	if (digestmode == NA && owned) {
		if (!junkboard() && currentuser->numposts > 0) {
			memcpy(&tmpu, currentuser, sizeof (struct userec));
			tmpu.numposts--;
			updateuserec(&tmpu, usernum);
		}
	}
	if (digestmode == YEA) {
		char normaldir[STRLEN];
		struct fileheader normalfh;
		bzero(&normalfh, sizeof (struct fileheader));
		normalfh.filetime = fileinfo->filetime;
		digestmode = 0;
		setbdir(normaldir, currboard, digestmode);
		change_dir(normaldir, &normalfh, (void *) DIR_do_digest,
			   bp->total, 0, 0, NULL);
		digestmode = 1;
	}
	limit_cpu();
//              sleep(3);
	return DIRCHANGED;
}

static int sequent_ent;
static int
sequent_messages(fptr)
struct fileheader *fptr;
{
	static int idc;
	if (fptr == NULL) {
		idc = 0;
		return 0;
	}
	idc++;
	if (readpost) {
		if (idc < sequent_ent)
			return 0;
		if (!UNREAD(fptr, &brc))
			return 0;
		if (continue_flag != 0) {
			genbuf[0] = 'y';
		} else {
			prints
			    ("讨论区: '%s' 标题:\n\"%s\" posted by %s.\n",
			     currboard, fptr->title, fh2owner(fptr));
			getdata(3, 0, "读取 (Y/N/Quit) [Y]: ", genbuf, 5,
				DOECHO, YEA);
		}
		if (genbuf[0] != 'y' && genbuf[0] != 'Y' && genbuf[0] != '\0') {
			if (genbuf[0] == 'q' || genbuf[0] == 'Q') {
				clear();
				return QUIT;
			}
			clear();
			return 0;
		}
		setbfile(genbuf, currboard, fh2fname(fptr));
		strsncpy(quote_file, genbuf, sizeof (quote_file));
		strsncpy(quote_user, fh2owner(fptr), sizeof (quote_user));
		register_attach_link(board_attach_link, fptr);
#ifdef NOREPLY
		ansimore_withzmodem(genbuf, YEA, fptr->title);
#else
		ansimore_withzmodem(genbuf, NA, fptr->title);
		move(t_lines - 1, 0);
		clrtoeol();
		prints
		    ("\033[1;44;31m[连续读信]  \033[33m回信 R │ 结束 Q,← │下一封 ' ',↓ │^R 回信给作者                \033[m");
		continue_flag = 0;
		switch (egetch()) {
		case 'N':
		case 'Q':
		case 'n':
		case 'q':
		case KEY_LEFT:
			break;
		case 'Y':
		case 'R':
		case 'y':
		case 'r':
			/* Added by deardragon 1999.11.21 增加不可 RE 属性 */
			if (!(fptr->accessed & FH_NOREPLY))
				do_reply(fptr);
			else {
				move(3, 0);
				clrtobot();
				prints
				    ("\n\n    老大,有人不让你Re这篇文章啊!!!    ");
				pressreturn();
				clear();
			}
			/* Added End. */
		case ' ':
		case '\n':
		case KEY_DOWN:
			continue_flag = 1;
			break;
		case Ctrl('R'):
			post_reply(0, fptr, (char *) NULL);
			break;
		default:
			break;
		}
#endif
		register_attach_link(NULL, NULL);
		clear();
	}
	setbdir(genbuf, currboard, digestmode);
	SETREAD(fptr, &brc);
	return 0;
}

static int
clear_new_flag(ent, fileinfo, direct)
int ent;
struct fileheader *fileinfo;
char *direct;
{
	static int lastf;
	if (now_t - lastf <= 2 || (fileinfo->accessed & FH_ISTOP)) {
		clear_new_flag_quick(0);
	} else {
		clear_new_flag_quick(max
				     (fileinfo->filetime, fileinfo->edittime));
	}
	lastf = now_t;
	return PARTUPDATE;
}

static int
sequential_read(ent, fileinfo, direct)
int ent;
struct fileheader *fileinfo;
char *direct;
{
	readpost = 1;
	clear();
	return sequential_read2(ent);
}
 /*ARGSUSED*/ static int
sequential_read2(ent /*,fileinfo,direct */ )
int ent;
/*struct fileheader *fileinfo ;
char *direct ;*/
{
	char buf[STRLEN];
	sequent_messages((struct fileheader *) NULL);
	sequent_ent = ent;
	continue_flag = 0;
	setbdir(buf, currboard, digestmode);
	apply_record(buf, (void *) sequent_messages,
		     sizeof (struct fileheader));
	return FULLUPDATE;
}

/* Added by netty to handle post saving into (0)Announce */
int
Save_post(ent, fileinfo, direct)
int ent;
struct fileheader *fileinfo;
char *direct;
{
	if (!IScurrBM)
		return DONOTHING;
	return (a_Save("0Announce", currboard, fileinfo, NA));
}

/* Added by ylsdd */
static void
quickviewpost(ent, fileinfo, direct)
int ent;
struct fileheader *fileinfo;
char *direct;
{
	char buf[STRLEN * 2];
	int i, j, x, y;
	int attach = 0, has_attach;
	FILE *fp;
	getyx(&y, &x);
	directfile(buf, direct, fh2fname(fileinfo));
	move(t_lines - 8, 0);
	clrtobot();
	fp = fopen(buf, "r");
	if (fp == NULL)
		return;
	if (fileinfo->accessed & FH_ATTACHED)
		has_attach = 1;
	else
		has_attach = 0;
	prints
	    ("\033[1;32m作者: \033[33m%-12s \033[32m标题: \033[33m%-40s\033[0m",
	     fh2owner(fileinfo), fileinfo->title);
	for (i = 0, j = 0; j < 7 - has_attach; i++) {
		move(t_lines - 7 + j, 0);
		if (fgets(buf, sizeof (buf), fp) == NULL)
			break;
		if (i < 4)
			continue;
		if (!strncmp(buf, "begin 644 ", 10)) {
			char attachname[41], *p;
			attach = 1;
			strncpy(attachname, buf + 10, 40);
			attachname[40] = 0;
			p = strchr(attachname, '\n');
			if (p != NULL)
				*p = 0;
			p = strrchr(attachname, '.');
			if (p != NULL
			    && (!strcasecmp(p, ".bmp") || !strcasecmp(p, ".jpg")
				|| !strcasecmp(p, ".gif")
				|| !strcasecmp(p, ".jpeg")))
				prints
				    ("\033[m附图: %s \033[5m(用www方式阅读本文可以浏览此图片)\033[0m\n",
				     attachname);
			else
				prints
				    ("\033[m附件: %s \033[5m(用www方式阅读本文可以下载此附件)\033[0m\n",
				     attachname);
			j++;
		} else if (!strncmp(buf, "end", 3) && attach) {
			attach = 0;
			continue;
		}
		if (attach)
			continue;
		j++;
		prints("\033[0m%s", buf);
	}
	if (has_attach) {
		prints("http://%s/" SMAGIC "%s/con?B=%d&F=%s&N=%d&T=%d",
		       MY_BBS_DOMAIN, temp_sessionid, getbnum(currboard) - 1,
		       fh2fname(fileinfo), ent, feditmark(fileinfo));
		j++;
	}
	if (j < 6) {
		move(t_lines - 7 + j, 0);
		prints
		    ("==========================结束=============================");
	}
	fclose(fp);
	move(y, x);
	refresh();
}

/* Added by ylsdd */
static int
change_t_lines()
{
	extern void (*quickview) ();
	if (0)
		if (!IScurrBM)
			return DONOTHING;
	quickview = quickviewpost;
	return UPDATETLINE;
}
static int
post_saved()
{
	char fname[STRLEN], title[STRLEN];
	if (!IScurrBM)
		return DONOTHING;
	setuserfile(fname, "tmpsave");
	title[0] = 0;
	if (!file_isfile(fname)) {
		a_prompt(-1,
			 "请先至该讨论区将文章存入暂存档, 按<Enter>继续...",
			 title, 2);
		return PARTUPDATE;
	}
	a_prompt(-1, "请输入文件或目录之中文名称： ", title, 50);
	if (strlen(title) == 0)
		return PARTUPDATE;
	if (postfile(fname, currboard, title, 2) != -1) {
		a_prompt(-1,
			 "已经帮你将暂存档转贴至版面了, 按<Enter>继续...",
			 title, 2);
	}
	unlink(fname);
	return DIRCHANGED;
}

/* Added by netty to handle post saving into (0)Announce */
/* 修改以用于对个人精华区的支持, by ylsdd*/
int
Import_post(ent, fileinfo, direct)
int ent;
struct fileheader *fileinfo;
char *direct;
{
	if (!USERPERM(currentuser, (PERM_BOARDS | PERM_SYSOP))
	    && !USERPERM(currentuser, PERM_SPECIAL8))
		return FULLUPDATE;
	a_Import(direct, fileinfo, NA);
	change_dir(direct, fileinfo, (void *) DIR_do_import, ent, digestmode,
		   1, NULL);
	return FULLUPDATE;
}

int
check_notespasswd()
{
	FILE *pass;
	char passbuf[20], prepass[STRLEN];
	char buf[STRLEN];
	setvfile(buf, currboard, "notespasswd");
	if ((pass = fopen(buf, "r")) != NULL) {
		fgets(prepass, STRLEN, pass);
		fclose(pass);
		prepass[strlen(prepass) - 1] = '\0';
		getdata(2, 0, "请输入秘密备忘录密码: ", passbuf, 19, NOECHO,
			YEA);
		if (passbuf[0] == '\0' || passbuf[0] == '\n')
			return NA;
		if (!checkpasswd_des(prepass, passbuf)) {
			move(3, 0);
			prints("错误的秘密备忘录密码...");
			pressanykey();
			return NA;
		}
	}
	return YEA;
}

static int
show_b_secnote()
{
	char buf[256];
	clear();
	setvfile(buf, currboard, "secnotes");
	if (file_isfile(buf)) {
		if (!check_notespasswd())
			return FULLUPDATE;
		clear();
		ansimore(buf, NA);
	} else {
		move(3, 25);
		prints("此讨论区尚无「秘密备忘录」。");
	}
	pressanykey();
	return FULLUPDATE;
}

static int
show_b_note()
{
	clear();
	if (show_board_notes(currboard) == -1) {
		move(4, 30);
		prints("此讨论区尚无「备忘录」。");
	}
	show_small_bm(currboard);
	if (!strcmp(currboard, "deleterequest")) {
		move(1, 0);
		if (!bbsinfo.utmpshm->watchman)
			prints("政治性版面当前都处在解锁状态");
		else
			prints
			    ("政治性版面已锁定,晚于 %s 就不能发表文章了.解锁码: %d",
			     Ctime(bbsinfo.utmpshm->watchman),
			     bbsinfo.utmpshm->unlock % 10000);

	}
	return what_to_do();
}

static int
show_file_info(ent, fileinfo, direct)
int ent;
struct fileheader *fileinfo;
char *direct;
{
	struct boardmem *bp;
	time_t t = fileinfo->filetime;
	bp = getbcache(currboard);
	if (NULL == bp)
		return DONOTHING;
	clear();
	move(0, 0);
	prints("这篇文章的详细信息如下:\n");
	prints("版面名称:     %s\n", currboard);
	prints("本版版主:     %s\n", bp->header.bm[0]);
	prints("版内在线:     %d 人\n", bp->inboard);
	prints("文章序号:     %d\n", ent);
	prints("文章标题:     %s\n", fileinfo->title);
	prints("文章作者:     %s\n", fh2owner(fileinfo));
	prints("文章日期:     %s", ctime(&t));
	prints("文章等级:     %d 级（%d 人评分，总分 %d 分）\n",
	       fileinfo->staravg50 / 50, fileinfo->hasvoted,
	       fileinfo->staravg50 * fileinfo->hasvoted / 50);
	prints("文件大小:     %d 字节\n", bytenum(fileinfo->sizebyte));
	prints("URL 地址:\n");
	prints("http://%s/" SMAGIC "%s/%scon?B=%d&F=%s&T=%d\n",
	       MY_BBS_DOMAIN, temp_sessionid,
	       (digestmode == YEA) ? "g" : "", getbnum(currboard) - 1,
	       fh2fname(fileinfo), feditmark(fileinfo));
	return what_to_do();
}

int
what_to_do()
{
	int retv = FULLUPDATE;
	move(t_lines - 1, 0);
	prints("\033[m%s%s%s%s%s%s%s: ",
	       USERPERM(currentuser, PERM_BASIC) ? "(d)查字典" : "",
	       USERPERM(currentuser, PERM_BASIC) ? "(n)科技词典" :
	       "", "(u)查询网友",
	       USERPERM(currentuser, PERM_POST) ? "(m)书灯絮语" : "",
	       USERPERM(currentuser, PERM_CHAT) ? "(c)聊天室" : "",
	       USERPERM(currentuser, PERM_BASIC) ? "(o)好友名单" : "",
	       USERPERM(currentuser, PERM_BASIC) ? "(q)计算" : "");
	switch (igetkey()) {
	case 'd':
		if (USERPERM(currentuser, PERM_BASIC))
			x_dict();
		break;
	case 'n':
		if (USERPERM(currentuser, PERM_BASIC))
			x_ncce();
		break;
	case 'u':
		clear();
		prints("查询网友状态");
		t_query(NULL);
		break;
	case 'm':
		clear();
		move(0, 0);
		prints("发送站内信件");
		if (USERPERM(currentuser, PERM_POST))
			m_send(NULL);
		break;
	case 'c':
		if (USERPERM(currentuser, PERM_CHAT))
			ent_chat("2");
		break;
	case 'o':
		if (USERPERM(currentuser, PERM_BASIC)) {
			t_friend();
			retv = 999;
		}
		break;
	case 'q':
		if (USERPERM(currentuser, PERM_BASIC))
			x_quickcalc();
		break;
	}
	return retv;
}

static int
do_t_query()
{
	t_query(NULL);
	return FULLUPDATE;
}
static int
into_backnumber()
{
	int savemode = uinfo.mode;
	selectbacknumber();
	modify_user_mode(savemode);
	return 999;
}

int
into_announce()
{
	if (a_menusearch
	    ("0Announce", currboard, (USERPERM(currentuser, PERM_ANNOUNCE)
				      || USERPERM(currentuser, PERM_SYSOP)
				      || USERPERM(currentuser,
						  PERM_OBOARDS)) ? PERM_BOARDS :
	     0))
		return FULLUPDATE;
	return DONOTHING;
}
static int
into_my_Personal()
{
	Personal("*");
	return FULLUPDATE;
}
static int
select_Personal()
{
	char uident[STRLEN], cmd[STRLEN];
	move(0, 0);
	clrtoeol();
	usercomplete("您要看谁的文集？", uident);
	if (uident[0] == 0)
		return FULLUPDATE;
	sprintf(cmd, "$%.15s", uident);
	Personal(cmd);
	return FULLUPDATE;
}

#ifdef INTERNET_EMAIL
int
forward_post(ent, fileinfo, direct)
int ent;
struct fileheader *fileinfo;
char *direct;
{
	if (strcmp("guest", currentuser->userid) == 0)
		return DONOTHING;
	return (mail_forward(ent, fileinfo, direct));
}

int
forward_u_post(ent, fileinfo, direct)
int ent;
struct fileheader *fileinfo;
char *direct;
{
	if (strcmp("guest", currentuser->userid) == 0)
		return DONOTHING;
	return (mail_u_forward(ent, fileinfo, direct));
}

#endif

struct one_key read_comms[] = {
	{'_', underline_post, "禁止回复"},
	{'r', read_post, "阅读文章"},
	{'u', do_t_query, "查询用户"},
	{'d', del_post, "删除文章"},
	{'D', del_range, "区段删除"},
	{'m', mark_post, "M 文章"},
	{'t', markdel_post, "标记删除"},
	{'E', edit_post, "修改文章"},
	{'Y', UndeleteArticle, "恢复文章到版面"},
	{Ctrl('G'), marked_mode, "文摘模式"},
	{Ctrl('T'), thread_mode, "同主题模式"},
	{Ctrl('Y'), zmodem_sendfile, "ZMODEM 下载"},
	{'.', deleted_mode, "删除模式"},	//add by ylsdd
	{'>', junk_mode, "垃圾模式"},	//add by ylsdd
	{'g', digest_post, "G 文章"},
	{'L', show_allmsgs, "查看消息"},
	{'T', edit_title, "修改标题"},
	{'s', do_select, "快速切换讨论区"},
	{Ctrl('C'), do_cross, "转贴文章"},
	{Ctrl('P'), do_post, "发表文章"},
	{'c', t_friends, "查看好友"},	/*clear_new_flag,          youzi */
	{'o', sequential_read, "循序阅读新文章"},
#ifdef INTERNET_EMAIL
	{'F', forward_post, "寄回邮箱"},
	{'U', forward_u_post, "uuencode 寄回"},
	{Ctrl('R'), post_reply, "回信给原作者"},
#endif
	{'i', Save_post, "将文章存入暂存档"},
	{'I', Import_post, "将文章放入精华区"},
	{'R', b_results, "查看投票结果"},
	{'v', b_vote, "参加投票"},
	{'M', b_vote_maintain, "维护投票"},
	{'W', b_notes_edit, "编辑/删除备忘录"},
	{Ctrl('W'), b_notes_passwd, "设定备忘录密码"},
	{'h', mainreadhelp, "查看帮助"},
	{KEY_TAB, show_b_note, "查看一般备忘录"},
	{'z', show_b_secnote, "查看秘密备忘录"},
	{'x', into_announce, "进入精华区"},
	{'X', into_my_Personal, "进入自己的个人文集"},	/* ylsdd 2000.2.27 */
	{'a', auth_search_down, "向后搜索作者"},
	{'A', auth_search_up, "向前搜索作者"},
	{'/', t_search_down, "向后搜索标题"},
	{'?', t_search_up, "向前搜索标题"},
	{'\'', post_search_down, "向后搜索内容"},
	{'\"', post_search_up, "向前搜索内容"},
	{']', thread_down, "向后同主题"},
	{'[', thread_up, "向前同主题"},
	{Ctrl('D'), deny_user, "取消某人POST权利"},
	{Ctrl('A'), show_author, "作者简介"},
	{'n', SR_first_new, "主题未读的第一篇"},
	{'\\', SR_last, "最后一篇同主题文章"},
	{'=', SR_first, "第一篇同主题文章"},
	{'p', SR_read, "相同主题的阅读"},
	{Ctrl('U'), SR_author, "相同作者阅读"},
	{'b', SR_BMfunc, "相关主题特殊功能"},
	{'!', Q_Goodbye, "快速离站"},
	{'S', s_msg, "传送讯息"},
	{'f', clear_new_flag, "清除所有未读标记"},
	{'e', markspec_post, "标记选定"},
	{Ctrl('E'), import_spec, "将选定文章放入精华区"},
	{Ctrl('K'), post_saved, "将暂存档张贴"},
	{',', change_t_lines, "更改行数,即时显示内容"},
	{'K', moveintobacknumber, "将文章移入过刊"},
	{';', into_backnumber, "查看过刊"},
	{Ctrl('N'), clubmember, "俱乐部成员设置"},
	{'C', friend_author, "增加作者为好友"},
	{Ctrl('S'), select_Personal, "选择个人文集"},
	{Ctrl('X'), power_select, "超强文章操作"},
	{'Z', del_post_backup, "特殊删文"},	//如果是deleterequest版面，则表示完成任务
	{'B', allcanre_post, "设置话题"},
	{'l', show_file_info, "文章详细信息"},
	{'#', addtop_post, "将文章置顶"},
	{Ctrl('V'), show_board_info, "查看版面信息"},
	{Ctrl('Q'), do_collect, "将文章加入文摘"},
	{'\0', NULL, ""}
};

/*Add by SmallPig*/
static void
notepad()
{
	char tmpname[STRLEN], note1[4];
	char note[3][STRLEN - 4];
	char tmp[STRLEN];
	FILE *in;
	int i, n;
	time_t thetime = time(NULL);
	extern int talkrequest;
	clear();
	move(0, 0);
	prints("开始你的留言吧！大家正拭目以待....\n");
	sprintf(tmpname, "tmp/notepad.%s.%05d", currentuser->userid, uinfo.pid);
	if ((in = fopen(tmpname, "w")) != NULL) {
		for (i = 0; i < 3; i++)
			memset(note[i], 0, STRLEN - 4);
		while (1) {
			for (i = 0; i < 3; i++) {
				getdata(1 + i, 0, ": ", note[i], STRLEN - 5,
					DOECHO, NA);
				if (note[i][0] == '\0')
					break;
			}
			if (i == 0) {
				fclose(in);
				unlink(tmpname);
				return;
			}
			getdata(5, 0,
				"是否把你的大作放入留言版 (Y)是的 (N)不要 (E)再编辑 [Y]: ",
				note1, 3, DOECHO, YEA);
			if (note1[0] == 'e' || note1[0] == 'E')
				continue;
			else
				break;
		}
		if (note1[0] != 'N' && note1[0] != 'n') {
			sprintf(tmp, "\033[1;32m%s\033[37m（%.18s）",
				currentuser->userid, currentuser->username);
			fprintf(in,
				"\033[1;34m□\033[44m□□□□□□□□□□□□□□□□□\033[36m酸\033[32m甜\033[33m苦\033[31m辣\033[37m版\033[34m□□□□□□□□□□□□□□□\033[40m□\033[m\n");
			fprintf(in,
				"\033[1;34m□\033[32;44m %-48s\033[32m在 \033[36m%.19s\033[32m 离开时留下的话  \033[m\n",
				tmp, Ctime(thetime));
			for (n = 0; n < i; n++) {
				if (note[n][0] == '\0')
					break;
				fprintf(in,
					"\033[1;34m□\033[33;44m %-75.75s\033[1;34m\033[m \n",
					note[n]);
			}
			fprintf(in,
				"\033[1;34m□\033[44m ————————————————————————————————————— \033[m \n");
			catnotepad(in, "etc/notepad");
			fclose(in);
			crossfs_rename(tmpname, "etc/notepad");
		} else {
			fclose(in);
			unlink(tmpname);
		}
	}
	if (talkrequest) {
		talkreply();
	}
	clear();
	return;
}

int
Goodbye()
{
	char spbuf[STRLEN];
	int choose;
	alarm(0);
	if (strcmp(currentuser->userid, "guest") && count_uindex(usernum) == 1) {
		if (USERDEFINE(currentuser, DEF_MAILMSG)) {
			if (get_msgcount(0, currentuser->userid) > 0)
				show_allmsgs();
		} else {
			clear_msg(currentuser->userid);
		}
	}

	*quote_file = '\0';
	move(1, 0);
	clear();
	move(0, 0);
	prints("你就要离开 %s ，可有什么建议吗？\n", MY_BBS_NAME);
	prints("[\033[1;33m1\033[m] 寄信给管理人员\n");
	prints("[\033[1;33m2\033[m] 按错了啦，我还要玩\n");
#ifdef USE_NOTEPAD
	if (strcmp(currentuser->userid, "guest") != 0) {
		prints
		    ("[\033[1;33m3\033[m] 写写\033[1;32m留\033[33m言\033[35m版\033[m罗\n");
	}
#endif
	prints("[\033[1;33m4\033[m] 不寄罗，要离开啦\n");
	sprintf(spbuf, "你的选择是 [\033[1;32m4\033[m]：");
	getdata(7, 0, spbuf, genbuf, 4, DOECHO, YEA);
	clear();
	choose = genbuf[0] - '0';
	if (choose == 1) {
		/*
		   if (!strcmp(currentuser->userid, "guest")) {
		   prints("先注册再给管理员写信吧。\n");
		   pressanykey();
		   } else {
		 */
		m_send("SYSOP");
		//      }
	}
	if (choose == 2)
		return FULLUPDATE;
#ifdef USE_NOTEPAD
	if (strcmp(currentuser->userid, "guest") != 0) {
		if (choose == 3 && USERPERM(currentuser, PERM_POST))
			notepad();
	}
#endif

	return Q_Goodbye();
}

int
Info()
{
	modify_user_mode(XMENU);
	ansimore("Version.Info", YEA);
	clear();
	return 0;
}

int
Conditions()
{
	modify_user_mode(XMENU);
	ansimore("COPYING", YEA);
	clear();
	return 0;
}

int
Welcome()
{
	char ans[3];
	modify_user_mode(XMENU);
	if (!file_isfile("etc/Welcome2"))
		ansimore("etc/Welcome", YEA);
	else {
		clear();
		stand_title("观看进站画面");
		for (;;) {
			getdata(1, 0,
				"(1)特殊进站公布栏  (2)本站进站画面 ? : ",
				ans, 2, DOECHO, YEA);
			if (ans[0] == '1' || ans[0] == '2')
				break;
		}
		if (ans[0] == '1')
			ansimore("etc/Welcome", YEA);
		else
			ansimore("etc/Welcome2", YEA);
	}
	clear();
	return 0;
}

int
cmpbnames(brec, bname)
struct boardheader *brec;
char *bname;
{
	if (!strncasecmp(bname, brec->filename, sizeof (brec->filename)))
		return 1;
	else
		return 0;
}

void
setbdir(char *buf, char *boardname, int Digestmode)
{
	char *dir = NULL;

	switch (Digestmode) {
	case NA:
		dir = DOT_DIR;
		break;
	case YEA:
		dir = DIGEST_DIR;
		break;
	case 2:
		dir = THREAD_DIR;
		break;
	case 3:
		dir = ".POWER.";
		break;
	case 4:
		dir = ".DELETED";
		break;
	case 5:
		dir = ".JUNK";
		break;
	}
	if (Digestmode == 3) {
		sprintf(buf, "boards/%s/%s%s%d", boardname, dir,
			currentuser->userid, uinfo.pid);
	} else
		sprintf(buf, "boards/%s/%s", boardname, dir);
}

int
zmodem_sendfile(ent, fileinfo, direct)
int ent;
struct fileheader *fileinfo;
char *direct;
{
	char *t;
	char buf1[512];

	strcpy(buf1, direct);
	if ((t = strrchr(buf1, '/')) != NULL)
		*t = '\0';
	snprintf(genbuf, 512, "%s/%s", buf1, fh2fname(fileinfo));
	return zsend_file(genbuf, fileinfo->title);
}

static int
Origin2(text)
char text[256];
{
	char tmp[STRLEN];

	sprintf(tmp, ":．%s %s．[FROM:", MY_BBS_NAME, email_domain());
	if (strstr(text, tmp))
		return 1;
	else
		return 0;
}

int
deleted_mode()
{
	extern char currdirect[STRLEN];

	if (!IScurrBM && !USERPERM(currentuser, PERM_ARBITRATE)
	    && !USERPERM(currentuser, PERM_SPECIAL5)) {
		return DONOTHING;
	}
	if (digestmode == 4) {
		digestmode = NA;
		setbdir(currdirect, currboard, digestmode);
	} else {
		digestmode = 4;
		setbdir(currdirect, currboard, digestmode);
		if (!file_isfile(currdirect)) {
			digestmode = NA;
			setbdir(currdirect, currboard, digestmode);
			return DONOTHING;
		}
	}
	return NEWDIRECT;
}

int
junk_mode()
{
	extern char currdirect[STRLEN];

	if (!USERPERM(currentuser, PERM_BLEVELS)) {
		return DONOTHING;
	}

	if (digestmode == 5) {
		digestmode = NA;
		setbdir(currdirect, currboard, digestmode);
	} else {
		digestmode = 5;
		setbdir(currdirect, currboard, digestmode);
		if (!file_isfile(currdirect)) {
			digestmode = NA;
			setbdir(currdirect, currboard, digestmode);
			return DONOTHING;
		}
	}
	return NEWDIRECT;
}

int
marked_mode()
{
	extern char currdirect[STRLEN];
	char ans[3];
	char whattosearch[31];
	char select[80];
	char direct[STRLEN];
	int type;

	if (digestmode == 3 || digestmode == YEA) {
		rmpowersymlink(currdirect);
		digestmode = NA;
		setbdir(currdirect, currboard, digestmode);
	} else {
		ans[0] = '\0';
		getdata(t_lines - 1, 0,
			"请选择:0)取消 1)文摘 2)m文章 3)防水 4)相关标题 5)同作者 6)垃圾桶 7)未读[1]:",
			ans, 2, DOECHO, NA);
		type = atoi(ans);
		if (ans[0] == '\0')
			type = 1;
		if (type < 1 || type > 8)
			return PARTUPDATE;
		whattosearch[0] = '\0';
		switch (type) {
		case 1:
			digestmode = YEA;
			break;
		case 3:
			sprintf(select, "标记含 m 或 标记含 g 或 属性是 原作");
			digestmode = NA;
			break;
		case 2:
			sprintf(select, "标记含 m");
			digestmode = NA;
			break;
		case 4:
			getdata(t_lines - 1, 0, "请输入标题关键字:",
				whattosearch, 31, DOECHO, NA);
			if (whattosearch[0] == '\0')
				return PARTUPDATE;
			sprintf(select, "标题含 %s", whattosearch);
			digestmode = NA;
			break;
		case 5:
			getdata(t_lines - 1, 0, "请输入作者ID:",
				whattosearch, 31, DOECHO, NA);
			if (whattosearch[0] == '\0')
				return PARTUPDATE;
			sprintf(select, "作者是 %s", whattosearch);
			digestmode = NA;
			break;
		case 6:
			sprintf(select, "作者是 %s", currentuser->userid);
			digestmode = 5;
			break;
		case 7:
			sprintf(select, "属性是 未读");
			digestmode = NA;
			break;
		case 8:
			sprintf(select, "作者是 %s", currentuser->userid);
			digestmode = 4;
			break;
		}
		if (type != 1) {
			setbdir(direct, currboard, digestmode);
			return power_action(direct, 1, -1, select, 9);
		}
		setbdir(currdirect, currboard, digestmode);
	}
	return NEWDIRECT;
}

/* end */

int
thread_mode()
{
	extern char currdirect[STRLEN];
	if (digestmode == 2) {
		digestmode = NA;
		setbdir(currdirect, currboard, digestmode);
	} else {
		digestmode = 2;
		setbdir(currdirect, currboard, digestmode);
		tracelog("%s thread %s", currentuser->userid, currboard);
		move(t_lines - 1, 0);
		clrtoeol();
		prints("\033[1;5m系统处理标题中, 请稍候...\033[m\n");
		refresh();
		do_thread(currboard);
		if (!file_isfile(currdirect)) {
			digestmode = NA;
			setbdir(currdirect, currboard, digestmode);
			return PARTUPDATE;
		}
	}
	return NEWDIRECT;
}

int
generate_title(char *fname, char *tname)
{
	struct fileheader mkpost, *ptr1, *ptr2;
	int fd, size = sizeof (struct fileheader), total, i, j, count =
	    0, hasht;
	char *t;
	struct mmapfile mf;
	struct hashstruct {
		int index, data;
	} *hashtable;
	int *index, *next;

	if ((fd = open(tname, O_WRONLY | O_CREAT, 0664)) == -1)
		return -1;	/* 创建文件发生错误 */
	flock(fd, LOCK_EX);

	index = NULL;
	hashtable = NULL;
	next = NULL;
	MMAP_TRY {
		if (mmapfile(fname, &mf) == -1) {
			flock(fd, LOCK_UN);
			close(fd);
			MMAP_RETURN(-1);
		}
		total = mf.size / size;
		hasht = total * 8 / 5;
		hashtable =
		    (struct hashstruct *) malloc(sizeof (*hashtable) * hasht);
		index = (int *) malloc(sizeof (int) * total);
		next = (int *) malloc(sizeof (int) * total);
		memset(hashtable, 0xFF, sizeof (*hashtable) * hasht);
		memset(index, 0, sizeof (int) * total);
		ptr1 = (struct fileheader *) (mf.ptr);
		for (i = 0; i < total; i++, ptr1++) {
			int l = 0, m;

			if (ptr1->thread == ptr1->filetime)
				l = i;
			else {
				l = ptr1->thread % hasht;
				while (hashtable[l].index != ptr1->thread
				       && hashtable[l].index != -1) {
					l++;
					if (l >= hasht)
						l = 0;
				}
				if (hashtable[l].index == -1)
					l = i;
				else
					l = hashtable[l].data;
			}
			if (l == i) {
				l = ptr1->thread % hasht;
				while (hashtable[l].index != -1) {
					l++;
					if (l >= hasht)
						l = 0;
				}
				hashtable[l].index = ptr1->thread;
				hashtable[l].data = i;
				index[i] = i;
				next[i] = 0;
			} else {
				m = index[l];
				next[m] = i;
				next[i] = 0;
				index[l] = i;
				index[i] = -1;
			}
		}
		ptr1 = (struct fileheader *) (mf.ptr);
		for (i = 0; i < total; i++, ptr1++)
			if (index[i] != -1) {

				write(fd, ptr1, size);
				count++;
				j = next[i];
				while (j != 0) {
					ptr2 =
					    (struct fileheader *) (mf.ptr +
								   j * size);
					memcpy(&mkpost, ptr2, sizeof (mkpost));
					t = ptr2->title;
					if (!strncmp(t, "Re:", 3))
						t += 4;
					sprintf(mkpost.title, "Re: %s", t);
					write(fd, &mkpost, size);
					count++;
					j = next[j];
				}
			}

		free(index);
		index = NULL;
		free(next);
		next = NULL;
		free(hashtable);
		hashtable = NULL;
	}
	MMAP_CATCH {
		flock(fd, LOCK_UN);
		close(fd);
		mmapfile(NULL, &mf);
		if (index)
			free(index);
		if (next)
			free(next);
		if (hashtable)
			free(hashtable);
		MMAP_RETURN(-1);
	}
	MMAP_END mmapfile(NULL, &mf);
	ftruncate(fd, count * size);
	flock(fd, LOCK_UN);
	close(fd);
	return 0;
}

static int
do_thread(char *board)
{
	char dname[STRLEN];
	char fname[STRLEN], tname[STRLEN];
	struct stat st1, st2;

	sprintf(dname, "boards/%s/%s", board, DOT_DIR);
	sprintf(fname, MY_BBS_HOME "/bbstmpfs/tmp/thread.%s.%s", board,
		DOT_DIR);
	sprintf(tname, "boards/%s/%s", board, THREAD_DIR);

	if (stat(dname, &st1) == -1)
		return -1;
	if (stat(tname, &st2) != -1) {
		if (st2.st_mtime >= st1.st_mtime)
			return -1;
	}

	unlink(tname);
	copyfile(dname, fname);
	generate_title(fname, tname);
	unlink(fname);
	return 0;
}

static int
b_notes_edit()
{
	char buf[STRLEN], buf2[STRLEN], *ptr;
	char ans[4], ch;
	int retv, editintro = 0;

	if (!IScurrBM) {
		return 0;
	}

	clear();
	move(1, 0);
	prints("编辑/删除备忘录");
	while (1) {
		getdata(3, 0,
			"编辑或删除本讨论区的 (0)离开 (1)一般备忘录 (2)秘密备忘录 (3)WWW版面简介? [1] ",
			ans, 2, DOECHO, YEA);
		ch = ans[0];
		if (ch == '0')
			return FULLUPDATE;
		if (ch == '\0')
			ch = '1';
		if (ch == '1' || ch == '2' || ch == '3')
			break;
	}

	makevdir(currboard);
	switch (ch) {
	case '3':
		setbfile(buf, currboard, "introduction");
		ptr = "WWW版面简介";
		editintro = 1;
		break;
	case '2':
		setvfile(buf, currboard, "secnotes");
		ptr = "秘密备忘录";
		break;
	default:
		setvfile(buf, currboard, "notes");
		ptr = "一般备忘录";
	}

	sprintf(buf2, "(E)编辑 (D)删除 %s? [E]: ", ptr);
	getdata(5, 0, buf2, ans, 2, DOECHO, YEA);
	if (ans[0] == 'D' || ans[0] == 'd') {
		move(6, 0);
		sprintf(buf2, "真的要删除%s", ptr);
		if (askyn(buf2, NA, NA)) {
			move(7, 0);
			prints("%s已经删除...\n", ptr);
			pressanykey();
			unlink(buf);
			if (ch == '2') {
				setvfile(buf, currboard, "notespasswd");
				unlink(buf);
			}
			retv = 1;
		} else
			retv = -1;
	} else {
		retv = vedit(buf, NA, YEA);
		if (editintro)
			updateintro(getbnum(currboard) - 1, buf);
	}
	if (retv == -1) {
		pressreturn();
	}
	return FULLUPDATE;
}

static int
b_notes_passwd()
{
	FILE *pass;
	char passbuf[20], prepass[20];
	char buf[STRLEN];

	if (!IScurrBM) {
		return 0;
	}
	clear();
	move(1, 0);
	prints("设定/更改「秘密备忘录」密码...");
	setvfile(buf, currboard, "secnotes");
	if (!file_isfile(buf)) {
		move(3, 0);
		prints("本讨论区尚无「秘密备忘录」。\n\n");
		prints("请先用 W 编好「秘密备忘录」再来设定密码...");
		pressanykey();
		return FULLUPDATE;
	}
	if (!check_notespasswd())
		return FULLUPDATE;
	getdata(3, 0, "请输入新的秘密备忘录密码: ", passbuf, 19, NOECHO, YEA);
	getdata(4, 0, "确认新的秘密备忘录密码: ", prepass, 19, NOECHO, YEA);
	if (strcmp(passbuf, prepass)) {
		prints("\n密码不相符, 无法设定或更改....");
		pressanykey();
		return FULLUPDATE;
	}
	setvfile(buf, currboard, "notespasswd");
	if (!passbuf[0]) {
		prints("\n密码为空，秘密备忘录密码被取消....");
		unlink(buf);
		pressanykey();
		return FULLUPDATE;
	}
	if ((pass = fopen(buf, "w")) == NULL) {
		move(5, 0);
		prints("备忘录密码无法设定....");
		pressanykey();
		return FULLUPDATE;
	}
	fprintf(pass, "%s\n", genpasswd_des(passbuf));
	fclose(pass);
	pass = 0;
	move(5, 0);
	prints("秘密备忘录密码设定完成....");
	pressanykey();
	return FULLUPDATE;
}

static int
catnotepad(fp, fname)
FILE *fp;
char *fname;
{
	char inbuf[256];
	FILE *sfp;
	int count;

	count = 0;
	if ((sfp = fopen(fname, "r")) == NULL) {
		fprintf(fp,
			"\033[1;34m  □\033[44m__________________________________________________________________________\033[m \n\n");
		return -1;
	}
	while (fgets(inbuf, sizeof (inbuf), sfp) != NULL) {
		if (count != 0)
			fputs(inbuf, fp);
		else
			count++;
	}
	fclose(sfp);
	return 0;
}
